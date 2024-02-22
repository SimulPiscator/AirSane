/*
AirSane Imaging Daemon
Copyright (C) 2018-2023 Simul Piscator

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "scanjob.h"
#include "imageformats/jpegencoder.h"
#include "imageformats/pdfencoder.h"
#include "imageformats/pngencoder.h"
#include "scanner.h"
#include "web/httpserver.h"
#include "basic/workerthread.h"

#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <regex>
#include <stdexcept>
#include <limits>

#include <sane/saneopts.h>

// pwg JobStateReasonsWKV
static const char* PWG_NONE = "None";
//static const char* PWG_SERVICE_OFF_LINE = "ServiceOffLine";
static const char* PWG_RESOURCES_ARE_NOT_READY = "ResourcesAreNotReady";
static const char* PWG_JOB_QUEUED = "JobQueued";
static const char* PWG_JOB_SCANNING = "JobScanning";
//static const char* PWG_JOB_SCANNING_AND_TRANSFERRING =
//  "JobScanningAndTransferring";
static const char* PWG_JOB_COMPLETED_SUCCESSFULLY = "JobCompletedSuccessfully";
static const char* PWG_JOB_CANCELED_BY_USER = "JobCanceledByUser";
static const char* PWG_INVALID_SCAN_TICKET = "InvalidScanTicket";
static const char* PWG_UNSUPPORTED_DOCUMENT_FORMAT =
  "UnsupportedDocumentFormat";
static const char* PWG_DOCUMENT_PERMISSION_ERROR = "DocumentPermissionError";
static const char* PWG_ERRORS_DETECTED = "ErrorsDetected";

namespace {

struct ScanSettingsXml
{
  ScanSettingsXml(const std::string& s)
    : xml(s)
  {}

  std::string getString(const std::string& name) const
  { // scan settings xml is simple enough to avoid using a parser
    std::regex r("<([a-zA-Z]+:" + name + ")>([^<]*)</\\1>");
    std::smatch m;
    if (std::regex_search(xml, m, r)) {
      assert(m.size() == 3);
      return m[2].str();
    }
    return "";
  }

  double getNumber(const std::string& name) const
  {
    return sanecpp::strtod_c(getString(name));
  }
  std::string xml;
};

template<class T> std::string describeStreamState(T& stream)
{
  if (stream.good())
      return "(good)";
  std::string state;
  if (stream.fail())
      state += "fail, ";
  if (stream.eof())
      state += "eof, ";
  if (stream.bad())
      state += "bad, ";
  if (!state.empty())
      state = state.substr(0, state.length() - 2);
  return "(" + state + ")";
}

}

struct ScanJob::Private
{
  void init(const ScanSettingsXml&, bool autoselectFormat, const OptionsFile::Options&);
  const char* kindString() const;
  void applyDeviceOptions(const OptionsFile::Options&);
  void initGammaTable(float gamma);
  void applyGamma(std::vector<char>&);
  void synthesizeGray(std::vector<char>&);
  const char* statusString() const;
  bool atomicTransition(State from, State to);
  void updateStatus(SANE_Status);

  SANE_Status openSession();
  void startSession();
  void closeSession();

  bool beginTransfer();
  void finishTransfer(std::ostream&);

  bool isPending() const;
  bool isProcessing() const;
  bool isFinished() const;
  bool isAborted() const;

  Scanner* mpScanner;

  std::string mUuid;
  std::atomic<::time_t> mCreated, mLastActive;
  std::atomic<State> mState;
  std::atomic<const char*> mStateReason;
  std::atomic<SANE_Status> mAdfStatus;

  std::string mScanSource, mIntent, mDocumentFormat, mColorMode;
  int mBitDepth, mRes_dpi;
  bool mColorScan;
  double mLeft_px, mTop_px, mWidth_px, mHeight_px;

  std::atomic<int> mKind, mImagesCompleted;
  std::shared_ptr<sanecpp::session> mpSession;

  OptionsFile::Options mDeviceOptions;
  std::vector<uint16_t> mGammaTable;

  // We need a job-permanent worker thread to execute
  // beginTransfer() and finishTransfer().
  // If these functions are called from two different
  // threads (e.g., requests for NextDocument), we get
  // into difficulties because backends are not required
  // to be thread safe.
  WorkerThread mWorkerThread;
};

ScanJob::ScanJob(Scanner* scanner, const std::string& uuid)
  : p(new Private)
{
  p->mpScanner = scanner;
  p->mCreated = ::time(nullptr);
  p->mLastActive = p->mCreated.load();
  p->mUuid = uuid;
  p->mState = pending;
  p->mStateReason = PWG_NONE;
  p->mAdfStatus = SANE_STATUS_GOOD;
}

ScanJob::~ScanJob()
{
  delete p;
}

ScanJob&
ScanJob::initWithScanSettingsXml(const std::string& xml, bool autoselect, const OptionsFile::Options& deviceOptions)
{
  p->init(ScanSettingsXml(xml), autoselect, deviceOptions);
  return *this;
}

int
ScanJob::ageSeconds() const
{
  return ::time(nullptr) - p->mCreated;
}

int
ScanJob::idleSeconds() const
{
  return ::time(nullptr) - p->mLastActive;
}

int
ScanJob::imagesCompleted() const
{
  return p->mImagesCompleted;
}

std::string
ScanJob::uri() const
{
  return p->mpScanner->uri() + "/ScanJobs/" + p->mUuid;
}

const std::string&
ScanJob::uuid() const
{
  return p->mUuid;
}

const std::string&
ScanJob::documentFormat() const
{
  return p->mDocumentFormat;
}

SANE_Status
ScanJob::adfStatus() const
{
  return p->mAdfStatus;
}

void
ScanJob::Private::init(const ScanSettingsXml& settings, bool autoselectFormat, const OptionsFile::Options& options)
{
  const char* err = nullptr;

  mIntent = settings.getString("Intent");
  if (mIntent.empty())
    mIntent = "Photo";

  double res_dpi = settings.getNumber("XResolution");
  if (!std::isnan(res_dpi) && res_dpi != settings.getNumber("YResolution"))
    err = PWG_INVALID_SCAN_TICKET;
  res_dpi = ::floor(res_dpi + 0.5);

  double left = settings.getNumber("XOffset"),
         top = settings.getNumber("YOffset"),
         width = settings.getNumber("Width"),
         height = settings.getNumber("Height");

  if (std::isnan(left))
    left = 0;
  if (std::isnan(top))
    top = 0;
  if (std::isnan(res_dpi))
    res_dpi = 300;

  double px_per_unit = 1.0;
  std::string units = settings.getString("ContentRegionUnits");
  if (units == "escl:ThreeHundredthsOfInches")
    px_per_unit = res_dpi / 300.0;

  mLeft_px = left * px_per_unit;
  mTop_px = top * px_per_unit;
  mWidth_px = width * px_per_unit;
  mHeight_px = height * px_per_unit;
  mRes_dpi = res_dpi;

  if (std::isnan(mWidth_px))
    mWidth_px = mpScanner->maxWidthPx300dpi();
  if (std::isnan(mHeight_px))
    mHeight_px = mpScanner->maxHeightPx300dpi();

  mBitDepth = 0;

  std::string esclColorMode = settings.getString("ColorMode");
  std::smatch m;
  if (std::regex_match(esclColorMode, m, std::regex("([A-Za-z]+)([0-9]+)"))) {
    assert(m.size() == 3);
    int esclBpp = atoi(m[2].str().c_str());
    auto c = m[1].str();
    if (c == "RGB") {
      mColorMode = mpScanner->colorScanModeName();
      mColorScan = true;
      mBitDepth = esclBpp / 3;
    } else if (c == "Grayscale") {
      mColorMode = mpScanner->grayScanModeName();
      mColorScan = false;
      mBitDepth = esclBpp;
    }
  }
  if (mColorMode.empty()) {
    if (mIntent == "Photo")
      mColorMode = mpScanner->colorScanModeName();
    else
      mColorMode = mpScanner->grayScanModeName();
    mBitDepth = 8;
  }

  mDocumentFormat = settings.getString("DocumentFormat");
  if (mDocumentFormat.empty())
      mDocumentFormat = settings.getString("DocumentFormatExt");
  if (!mDocumentFormat.empty())
     std::clog << "document format requested: " << mDocumentFormat << "\n";
  else if (mIntent == "Document" || mIntent == "Text")
     mDocumentFormat = HttpServer::MIME_TYPE_PDF;
  else if (mIntent == "Photo")
     mDocumentFormat = HttpServer::MIME_TYPE_JPEG;

  // If Apple Airscan requests JPEG, we send PNG instead because it is
  // lossless and supports all bit depths.
  if (mDocumentFormat.empty() || autoselectFormat)
    mDocumentFormat = HttpServer::MIME_TYPE_PNG;
  std::clog << "document format used: " << mDocumentFormat << "\n";

  mImagesCompleted = 0;

  std::string inputSource = settings.getString("InputSource");
  if (inputSource.empty()) {
    if (mpScanner->hasPlaten())
      inputSource = "Platen";
    else
      inputSource = "Feeder";
  }
  if (inputSource == "Platen") {
    mScanSource = mpScanner->platenSourceName();
    mKind = single;
  }
  else if (inputSource == "Feeder") {
    mScanSource = mpScanner->adfSourceName();
    double concatIfPossible = settings.getNumber("ConcatIfPossible");
    if (concatIfPossible == 1.0 && mDocumentFormat == HttpServer::MIME_TYPE_PDF)
      mKind = adfConcat;
    else
      mKind = adfSingle;
  }
  else {
    err = PWG_INVALID_SCAN_TICKET;
    std::cerr << "unknown input source: " << inputSource << std::endl;
  }
  std::clog << "job kind: " << kindString() << std::endl;

  applyDeviceOptions(options);

  if (err) {
    mState = aborted;
    mStateReason = err;
  } else {
    mState = pending;
    mStateReason = PWG_JOB_QUEUED;
  }
}

const char*
ScanJob::Private::kindString() const
{
  switch (mKind) {
    case single:
      return "single";
    case adfConcat:
      return "ADF concat";
    case adfSingle:
      return "ADF single";
  }
  return "unknown";
}

void
ScanJob::Private::applyDeviceOptions(const OptionsFile::Options& options)
{
  mDeviceOptions = options;
  mGammaTable.clear();
  if (!mColorScan) {
    std::clog << "using grayscale gamma of " << options.gray_gamma << std::endl;
    initGammaTable(options.gray_gamma);
  }
  else {
    std::clog << "using color gamma of " << options.color_gamma << std::endl;
    initGammaTable(options.color_gamma);
  }
  if (!mColorScan) {
    if (options.synthesize_gray) {
      std::clog << "synthesizing grayscale from RGB" << std::endl;
      mColorMode = mpScanner->colorScanModeName();
    }
    else {
      std::clog << "requesting grayscale from backend" << std::endl;
      mColorMode = mpScanner->grayScanModeName();
    }
  }
}

void
ScanJob::Private::initGammaTable(float gammaVal)
{
  mGammaTable.clear();
  if (gammaVal == 1.0f)
    return;
  int size = 1L << mBitDepth;
  float scale = 1.0f / (size - 1), invscale = size - 1;
  mGammaTable.resize(size);
  for (int i = 0; i < size; ++i) {
    float f = i * scale;
    f = ::pow(f, gammaVal);
    f *= invscale;
    f = ::floor(f + 0.5);
    mGammaTable[i] = f;
  }
}

void
ScanJob::Private::applyGamma(std::vector<char>& ioData)
{
  union
  {
    char* c;
    uint8_t* b;
    uint16_t* s;
  } data = { ioData.data() };
  if (mGammaTable.size() == 1 << 8) {
    for (size_t i = 0; i < ioData.size(); ++i)
      data.b[i] = mGammaTable[data.b[i]];
  } else if (mGammaTable.size() == 1 << 16) {
    for (size_t i = 0; i < ioData.size() / 2; ++i)
      data.s[i] = mGammaTable[data.s[i]];
  }
}

void
ScanJob::Private::synthesizeGray(std::vector<char>& ioData)
{
  // sRGB spectral weightings
  static const float rweight = 0.2126f, gweight = 0.7152f, bweight = 0.0722f;
  union
  {
    char* c;
    uint8_t* b;
    uint16_t* s;
  } in = { ioData.data() }, out = in;
  if (mBitDepth == 8) {
    while (in.c < ioData.data() + ioData.size()) {
      float f = rweight * in.b[0] + gweight * in.b[1] + bweight * in.b[2];
      f += 0.5f;
      f = std::min(255.0f, f);
      *out.b = f;
      in.b += 3;
      out.b += 1;
    }
  } else if (mBitDepth == 16) {
    while (in.c < ioData.data() + ioData.size()) {
      float f = rweight * in.s[0] + gweight * in.s[1] + bweight * in.s[2];
      f += 0.5f;
      f = std::min(65535.0f, f);
      *out.s = f;
      in.s += 3;
      out.s += 1;
    }
  }
}

const char*
ScanJob::Private::statusString() const
{
  switch (mState.load()) {
    case ScanJob::aborted:
      return "Aborted";
    case ScanJob::canceled:
      return "Canceled";
    case ScanJob::completed:
      return "Completed";
    case ScanJob::pending:
      return "Pending";
    case ScanJob::processing:
      return "Processing";
  }
  return "";
}

bool
ScanJob::Private::atomicTransition(State from, State to)
{
  return mState.compare_exchange_strong(from, to);
}

void
ScanJob::Private::updateStatus(SANE_Status status)
{
  mAdfStatus = SANE_STATUS_GOOD;
  switch (status) {
    case SANE_STATUS_GOOD:
      mState = processing;
      mStateReason = PWG_JOB_SCANNING;
      break;
    case SANE_STATUS_INVAL:
      mState = aborted;
      mStateReason = PWG_INVALID_SCAN_TICKET;
      break;
    case SANE_STATUS_DEVICE_BUSY:
    case SANE_STATUS_IO_ERROR:
    case SANE_STATUS_NO_MEM:
      mState = aborted;
      mStateReason = PWG_RESOURCES_ARE_NOT_READY;
      break;
    case SANE_STATUS_ACCESS_DENIED:
      mState = aborted;
      mStateReason = PWG_DOCUMENT_PERMISSION_ERROR;
      break;
    case SANE_STATUS_JAMMED:
    case SANE_STATUS_COVER_OPEN:
      mState = aborted;
      mStateReason = PWG_RESOURCES_ARE_NOT_READY;
      mAdfStatus = status;
      break;
    case SANE_STATUS_CANCELLED:
      mState = aborted;
      mStateReason = PWG_JOB_CANCELED_BY_USER;
      break;
    case SANE_STATUS_EOF:
      switch(mKind) {
      case single:
          if (mImagesCompleted > 0) {
              mState = completed;
              mStateReason = PWG_JOB_COMPLETED_SUCCESSFULLY;
          } else {
            mState = pending;
            mStateReason = PWG_NONE;
          }
          closeSession();
          break;
      case adfSingle:
          mState = pending;
          mStateReason = PWG_NONE;
          break;
      case adfConcat:
          updateStatus(mpSession->start().status());
          break;
      }
      break;
    case SANE_STATUS_NO_DOCS:
      if (mImagesCompleted > 0 && (mKind == adfSingle || mKind == adfConcat)) {
        mState = completed;
        mStateReason = PWG_JOB_COMPLETED_SUCCESSFULLY;
      } else {
        mState = aborted;
        mStateReason = PWG_RESOURCES_ARE_NOT_READY;
      }
      mAdfStatus = status;
      closeSession();
      break;
    default:
      mState = aborted;
      mStateReason = PWG_ERRORS_DETECTED;
  }
  if (mState == aborted)
    closeSession();
}

void
ScanJob::writeJobInfoXml(std::ostream& os) const
{
  os << "<scan:JobInfo>\r\n"
        "<pwg:JobUri>"
     << uri()
     << "</pwg:JobUri>\r\n"
        "<pwg:JobUuid>"
     << uuid()
     << "</pwg:JobUuid>\r\n"
        "<scan:Age>"
     << ::time(nullptr) - p->mCreated
     << "</scan:Age>\r\n"
        "<pwg:JobState>"
     << p->statusString()
     << "</pwg:JobState>\r\n"
        "<pwg:ImagesCompleted>"
     << p->mImagesCompleted
     << "</pwg:ImagesCompleted>\r\n"
        "<pwg:JobStateReasons>\r\n"
        "<pwg:JobStateReason>"
     << p->mStateReason
     << "</pwg:JobStateReason>\r\n"
        "</pwg:JobStateReasons>\r\n"
        "</scan:JobInfo>\r\n";
}

bool
ScanJob::beginTransfer()
{
  struct : WorkerThread::Callable
  {
    void onCall() override
    {
      result = p->beginTransfer();
    }
    bool result = false;
    Private* p = nullptr;
  } functionCall;
  functionCall.p = p;
  p->mWorkerThread.executeSynchronously(functionCall);
  return functionCall.result;
}

bool
ScanJob::Private::beginTransfer()
{
  if(!atomicTransition(pending, processing))
    return false;
  bool ok = true;
  if (!mpSession) {
    ok = (openSession() == SANE_STATUS_GOOD);
    if (ok)
      mpSession->dump_options();
  }
  startSession();
  ok = isProcessing();
  if (!ok)
    closeSession();
  return ok;
}

SANE_Status
ScanJob::Private::openSession()
{
  SANE_Status status = SANE_STATUS_GOOD;
  assert(!mpSession);
  mpSession = mpScanner->open();
  status = mpSession->status();
  if (status == SANE_STATUS_GOOD) {

    auto& opt = mpSession->options();

    for (const auto& option : mDeviceOptions.sane_options)
      opt[option.first] = option.second;

    // The order in which options are set matters for some backends.
    opt[SANE_NAME_SCAN_SOURCE] = mScanSource;
    opt[SANE_NAME_SCAN_MODE] = mColorMode;
    opt[SANE_NAME_BIT_DEPTH] = mBitDepth;
    bool ok = opt[SANE_NAME_SCAN_RESOLUTION].set_numeric_value(mRes_dpi);
    if (!ok)
      ok = opt[SANE_NAME_SCAN_X_RESOLUTION].set_numeric_value(mRes_dpi) ||
           opt[SANE_NAME_SCAN_Y_RESOLUTION].set_numeric_value(mRes_dpi);

    double left = mLeft_px, top = mTop_px, right = mLeft_px + mWidth_px,
           bottom = mTop_px + mHeight_px;

    switch (opt[SANE_NAME_SCAN_TL_X].unit()) {
      case SANE_UNIT_PIXEL:
        break;
      case SANE_UNIT_MM:
        for (auto p : { &left, &right, &top, &bottom })
          *p *= 25.4 / mRes_dpi;
        break;
      default:
        ok = false;
    }
    for (auto p : { &left, &right, &top, &bottom })
      *p = ::floor(*p + 0.5);
    opt[SANE_NAME_SCAN_TL_X] = left;
    opt[SANE_NAME_SCAN_TL_Y] = top;
    opt[SANE_NAME_SCAN_BR_X] = right;
    opt[SANE_NAME_SCAN_BR_Y] = bottom;

    if (!ok)
      status = SANE_STATUS_INVAL;
  }
  return status;
}

void
ScanJob::Private::startSession()
{
  SANE_Status status = mpSession->start().status();
  updateStatus(status);
}

void
ScanJob::Private::closeSession()
{
  if (mpSession)
    mpSession->cancel();
  mpSession.reset();
}

ScanJob&
ScanJob::finishTransfer(std::ostream& os)
{
  struct : WorkerThread::Callable
  {
    void onCall() override
    {
      p->finishTransfer(*pOs);
    }
    Private* p = nullptr;
    std::ostream* pOs = nullptr;
  } functionCall;
  functionCall.p = p;
  functionCall.pOs = &os;
  p->mWorkerThread.executeSynchronously(functionCall);
  return *this;
}

void
ScanJob::Private::finishTransfer(std::ostream& os)
{
  mLastActive = ::time(nullptr);
  std::shared_ptr<ImageEncoder> pEncoder;
  if (isProcessing()) {
    if (mDocumentFormat == HttpServer::MIME_TYPE_JPEG) {
      auto jpegEncoder = new JpegEncoder;
      jpegEncoder->setGamma(1.0);
      jpegEncoder->setQualityPercent(90);
      pEncoder.reset(jpegEncoder);
    } else if (mDocumentFormat == HttpServer::MIME_TYPE_PDF) {
      auto pdfEncoder = new PdfEncoder;
#if 0 // "Title" does not conform to pdf/raster
      pdfEncoder->documentInfo()["Title"] =
        mUuid + "/" + sanecpp::dtostr_c(mImagesCompleted);
#endif
      pdfEncoder->documentInfo()["Creator"] =
        mpScanner->makeAndModel() + " (SANE)";
      pdfEncoder->documentInfo()["Producer"] = "AirSane Server";
      pEncoder.reset(pdfEncoder);
    } else if (mDocumentFormat == HttpServer::MIME_TYPE_PNG) {
      auto pngEncoder = new PngEncoder;
      pEncoder.reset(pngEncoder);
    } else {
      mState = aborted;
      mStateReason = PWG_UNSUPPORTED_DOCUMENT_FORMAT;
    }
  }
  if (isProcessing()) {
    pEncoder->setResolutionDpi(mRes_dpi);
    if (mColorScan)
      pEncoder->setColorspace(ImageEncoder::RGB);
    else
      pEncoder->setColorspace(ImageEncoder::Grayscale);
    auto p = mpSession->parameters();
    pEncoder->setWidth(p->pixels_per_line);
    pEncoder->setHeight(p->lines);
    pEncoder->setBitDepth(p->depth);
    pEncoder->setDestination(&os);
    if (!mColorScan && mDeviceOptions.synthesize_gray) {
      if (pEncoder->bytesPerLine() != p->bytes_per_line / 3) {
            std::cerr << __FILE__ << ", line " << __LINE__
                      << ": encoder bytesPerLine (" << pEncoder->bytesPerLine()
                      << ") differs from SANE bytes_per_line/3 ("
                      << p->bytes_per_line / 3 << ")" << std::endl;
            mState = aborted;
            mStateReason = PWG_ERRORS_DETECTED;
      }
    } else if (pEncoder->bytesPerLine() != p->bytes_per_line) {
      std::cerr << __FILE__ << ", line " << __LINE__
                << ": encoder bytesPerLine (" << pEncoder->bytesPerLine()
                << ") differs from SANE bytes_per_line (" << p->bytes_per_line
                << ")" << std::endl;
      mState = aborted;
      mStateReason = PWG_ERRORS_DETECTED;
    }
  }
  while (isProcessing()) {
    int linesWritten = 0;
    mLastActive = ::time(nullptr);
    std::vector<char> buffer(mpSession->parameters()->bytes_per_line);
    SANE_Status status = SANE_STATUS_GOOD;
    while (status == SANE_STATUS_GOOD && os && isProcessing()) {
      status = mpSession->read(buffer).status();
      mLastActive = ::time(nullptr);
      if (status == SANE_STATUS_GOOD) {
        applyGamma(buffer);
        if (!mColorScan && mDeviceOptions.synthesize_gray)
          synthesizeGray(buffer);
        try {
          pEncoder->writeLine(buffer.data());
          ++linesWritten;
          if (!os.flush())
            throw std::runtime_error("Could not send data, state: " + describeStreamState(os));
        } catch (const std::runtime_error& e) {
          std::cerr << e.what() << ", aborting" << std::endl;
          mState = aborted;
          mStateReason = PWG_ERRORS_DETECTED;
          closeSession();
        }
      }
    }
    std::clog << "lines written: " << linesWritten << std::endl;
    if (isProcessing()) {
      ++mImagesCompleted;
      std::clog << "images completed: " << mImagesCompleted << std::endl;
      updateStatus(status);
      if (pEncoder->linesLeftInCurrentImage() != pEncoder->height()) {
        std::cerr << "incomplete or excess scan data" << std::endl;
        mState = aborted;
        mStateReason = PWG_ERRORS_DETECTED;
      }
    }
  }
  if (pEncoder)
      pEncoder->endDocument();
  mLastActive = ::time(nullptr);
}

ScanJob&
ScanJob::cancel()
{
  p->mState = canceled;
  p->mStateReason = PWG_JOB_CANCELED_BY_USER;
  p->closeSession();
  return *this;
}

ScanJob::State
ScanJob::state() const
{
  return p->mState;
}

std::string
ScanJob::statusString() const
{
  return p->statusString();
}

std::string
ScanJob::statusReason() const
{
  return std::string(p->mStateReason);
}

bool
ScanJob::isPending() const
{
  return p->isPending();
}

bool
ScanJob::isProcessing() const
{
  return p->isProcessing();
}

bool
ScanJob::isFinished() const
{
  return p->isFinished();
}

bool
ScanJob::isAborted() const
{
  return p->isAborted();
}

bool
ScanJob::Private::isPending() const
{
  return mState == pending;
}

bool
ScanJob::Private::isProcessing() const
{
  return mState == processing;
}

bool
ScanJob::Private::isFinished() const
{
  switch (mState.load()) {
    case pending:
    case processing:
      return false;
    case aborted:
    case canceled:
    case completed:
      return true;
  }
  return true;
}

bool
ScanJob::Private::isAborted() const
{
  return mState == aborted;
}
