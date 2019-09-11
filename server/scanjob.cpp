/*
AirSane Imaging Daemon
Copyright (C) 2018 Simul Piscator

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
#include "scanner.h"
#include "imageformats/jpegencoder.h"
#include "imageformats/pdfencoder.h"
#include "imageformats/pngencoder.h"
#include "web/httpserver.h"
#include <sane/saneopts.h>
#include <regex>
#include <atomic>
#include <cmath>
#include <cassert>

// pwg JobStateReasonsWKV
static const char* PWG_NONE = "None";
static const char* PWG_SERVICE_OFF_LINE = "ServiceOffLine";
static const char* PWG_RESOURCES_ARE_NOT_READY = "ResourcesAreNotReady";
static const char* PWG_JOB_QUEUED = "JobQueued";
static const char* PWG_JOB_SCANNING = "JobScanning";
static const char* PWG_JOB_SCANNING_AND_TRANSFERRING = "JobScanningAndTransferring";
static const char* PWG_JOB_COMPLETED_SUCCESSFULLY = "JobCompletedSuccessfully";
static const char* PWG_JOB_CANCELED_BY_USER = "JobCanceledByUser";
static const char* PWG_INVALID_SCAN_TICKET = "InvalidScanTicket";
static const char* PWG_UNSUPPORTED_DOCUMENT_FORMAT = "UnsupportedDocumentFormat";
static const char* PWG_DOCUMENT_PERMISSION_ERROR = "DocumentPermissionError";
static const char* PWG_ERRORS_DETECTED = "ErrorsDetected";

namespace {

struct ScanSettingsXml
{
    ScanSettingsXml(const std::string& s) : xml(s) {}

    std::string getString(const std::string& name) const
    {   // scan settings xml is simple enough to avoid using a parser
        std::regex r("<([a-zA-Z]+:" + name + ")>([^<]*)</\\1>");
        std::smatch m;
        if(std::regex_search(xml, m, r)) {
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

}

struct ScanJob::Private
{
    void init(const ScanSettingsXml&, bool autoselectFormat);
    const char* statusString() const;
    bool atomicTransition(State from, State to);
    void updateStatus(SANE_Status);
    void start();
    void abortTransfer();
    void finishTransfer(std::ostream&);

    bool isPending() const;
    bool isProcessing() const;
    bool isFinished() const;
    bool isAborted() const;

    Scanner* mpScanner;

    std::string mUuid;
    ::time_t mCreated;
    std::atomic<State> mState;
    std::atomic<const char*> mStateReason;

    std::string mScanSource, mIntent, mDocumentFormat, mColorMode;
    int mBitDepth, mRes_dpi;
    double mLeft_px, mTop_px, mWidth_px, mHeight_px;

    int mImagesToTransfer, mImagesCompleted;
    std::shared_ptr<sanecpp::session> mpSession;

};

ScanJob::ScanJob(Scanner* scanner, const std::string& uuid)
    : p(new Private)
{
    p->mpScanner = scanner;
    p->mCreated = ::time(nullptr);
    p->mUuid = uuid;
    p->mState = pending;
    p->mStateReason = PWG_NONE;
}

ScanJob::~ScanJob()
{
    delete p;
}

ScanJob &ScanJob::initWithScanSettingsXml(const std::string &xml, bool autoselect)
{
    p->init(ScanSettingsXml(xml), autoselect);
    return *this;
}

int ScanJob::ageSeconds() const
{
    return ::time(nullptr) - p->mCreated;
}

int ScanJob::imagesToTransfer() const
{
    return p->mImagesToTransfer;
}

int ScanJob::imagesCompleted() const
{
    return p->mImagesCompleted;
}

std::string ScanJob::uri() const
{
   return p->mpScanner ? p->mpScanner->uri() + "/ScanJobs/" + p->mUuid : "";
}

const std::string& ScanJob::uuid() const
{
    return p->mUuid;
}

const std::string &ScanJob::documentFormat() const
{
    return p->mDocumentFormat;
}

void ScanJob::Private::init(const ScanSettingsXml& settings, bool autoselectFormat)
{
    const char* err = nullptr;

    double res_dpi = settings.getNumber("XResolution");
    if(res_dpi != settings.getNumber("YResolution"))
        err = PWG_INVALID_SCAN_TICKET;
    res_dpi = ::floor(res_dpi + 0.5);

    double
        left = settings.getNumber("XOffset"),
        top = settings.getNumber("YOffset"),
        width = settings.getNumber("Width"),
        height = settings.getNumber("Height");

    if(std::isnan(left))
        left = 0;
    if(std::isnan(top))
        top = 0;

    if(std::isnan(width) || std::isnan(height) || std::isnan(res_dpi))
        err = PWG_INVALID_SCAN_TICKET;

    double px_per_unit = 1.0;
    std::string units = settings.getString("ContentRegionUnits");
    if(units == "escl:ThreeHundredthsOfInches")
        px_per_unit = res_dpi/300.0;
    else
        err = PWG_INVALID_SCAN_TICKET;

    mLeft_px = left * px_per_unit;
    mTop_px = top * px_per_unit;
    mWidth_px = width * px_per_unit;
    mHeight_px = height * px_per_unit;
    mRes_dpi = res_dpi;

    mBitDepth = 0;

    std::string esclColorMode = settings.getString("ColorMode");
    std::smatch m;
    if(std::regex_match(esclColorMode, m, std::regex("([A-Za-z]+)([0-9]+)"))) {
        assert(m.size() == 3);
        int esclBpp = atoi(m[2].str().c_str());
        auto c = m[1].str();
        if(c == "RGB") {
            mColorMode = mpScanner->colorScanModeName();
            mBitDepth = esclBpp/3;
        }
        else if(c == "Grayscale") {
            mColorMode = mpScanner->grayScanModeName();
            mBitDepth = esclBpp;
        }
    }
    if(mColorMode.empty())
        err = PWG_INVALID_SCAN_TICKET;

    mIntent = settings.getString("Intent");
    mDocumentFormat = settings.getString("DocumentFormat");
    if(autoselectFormat && mDocumentFormat == HttpServer::MIME_TYPE_JPEG
            && (mRes_dpi > 75 && mBitDepth > 8))
        mDocumentFormat = HttpServer::MIME_TYPE_PNG;
    mImagesToTransfer = 1;
    mImagesCompleted = 0;

    std::string inputSource = settings.getString("InputSource");
    if(inputSource == "Platen")
        mScanSource = mpScanner->platenSourceName();
    else if(inputSource == "Feeder")
        mScanSource = mpScanner->adfSourceName();

    if(err) {
        mState = aborted;
        mStateReason = err;
    } else {
        mState = pending;
        mStateReason = PWG_JOB_QUEUED;
    }
}

const char* ScanJob::Private::statusString() const
{
    switch(mState) {
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

bool ScanJob::Private::atomicTransition(State from, State to)
{
    return mState.compare_exchange_strong(from, to);
}

void ScanJob::Private::updateStatus(SANE_Status status)
{
    switch(status) {
    case SANE_STATUS_GOOD:
        mState = processing;
        mStateReason = PWG_JOB_SCANNING;
        break;
    case SANE_STATUS_DEVICE_BUSY:
        mState = pending;
        mStateReason = PWG_NONE;
        break;
    case SANE_STATUS_INVAL:
        mState = aborted;
        mStateReason = PWG_INVALID_SCAN_TICKET;
        break;
    case SANE_STATUS_IO_ERROR:
    case SANE_STATUS_NO_MEM:
        mState = aborted;
        mStateReason = PWG_RESOURCES_ARE_NOT_READY;
        break;
    case SANE_STATUS_ACCESS_DENIED:
        mState = aborted;
        mStateReason = PWG_DOCUMENT_PERMISSION_ERROR;
        break;
    case SANE_STATUS_NO_DOCS:
    case SANE_STATUS_EOF:
        if(mImagesCompleted == mImagesToTransfer) {
            mState = completed;
            mStateReason = PWG_JOB_COMPLETED_SUCCESSFULLY;
        } else {
            mState = pending;
            mStateReason = PWG_NONE;
        }
        break;
    default:
        mState = aborted;
        mStateReason = PWG_ERRORS_DETECTED;
    }
}

void ScanJob::writeJobInfoXml(std::ostream &os) const
{
    os <<
    "<scan:JobInfo>\r\n"
        "<pwg:JobUri>" << uri() << "</pwg:JobUri>\r\n"
        "<pwg:JobUuid>" << uuid() << "</pwg:JobUuid>\r\n"
        "<scan:Age>" << ::time(nullptr) - p->mCreated << "</scan:Age>\r\n"
        "<pwg:JobState>" << p->statusString() << "</pwg:JobState>\r\n"
        "<pwg:ImagesToTransfer>" << p->mImagesToTransfer << "</pwg:ImagesToTransfer>\r\n"
        "<pwg:ImagesCompleted>" << p->mImagesCompleted << "</pwg:ImagesCompleted>\r\n"
        "<pwg:JobStateReasons>\r\n"
            "<pwg:JobStateReason>" << p->mStateReason << "</pwg:JobStateReason>\r\n"
        "</pwg:JobStateReasons>\r\n"
    "</scan:JobInfo>\r\n";
}

bool ScanJob::beginTransfer()
{
    if(!p->atomicTransition(pending, processing))
        return false;
    p->start();
    return isProcessing();
}

ScanJob &ScanJob::abortTransfer()
{
    p->abortTransfer();
    return *this;
}

void ScanJob::Private::start()
{
    assert(!mpSession);
    mpSession = mpScanner->open();
    auto& opt = mpSession->options();
    if(mIntent == "Preview")
        opt[SANE_NAME_PREVIEW] = 1;
    opt[SANE_NAME_BIT_DEPTH] = mBitDepth;
    opt[SANE_NAME_SCAN_MODE] = mColorMode;
    opt[SANE_NAME_SCAN_SOURCE] = mScanSource;
    bool ok = opt[SANE_NAME_SCAN_RESOLUTION].set_numeric_value(mRes_dpi);
    if(!ok)
       ok = opt[SANE_NAME_SCAN_X_RESOLUTION].set_numeric_value(mRes_dpi)
                || opt[SANE_NAME_SCAN_Y_RESOLUTION].set_numeric_value(mRes_dpi);

    double left = mLeft_px, top = mTop_px,
            right = mLeft_px + mWidth_px,
            bottom = mTop_px + mHeight_px;

    switch(opt[SANE_NAME_SCAN_TL_X].unit()) {
    case SANE_UNIT_PIXEL:
        break;
    case SANE_UNIT_MM:
        for(auto p : { &left, &right, &top, &bottom })
            *p *= 25.4/mRes_dpi;
        break;
    default:
        ok = false;
    }
    for(auto p : { &left, &right, &top, &bottom })
        *p = ::floor(*p + 0.5);
    opt[SANE_NAME_SCAN_TL_X] = left;
    opt[SANE_NAME_SCAN_TL_Y] = top;
    opt[SANE_NAME_SCAN_BR_X] = right;
    opt[SANE_NAME_SCAN_BR_Y] = bottom;

    SANE_Status status = SANE_STATUS_INVAL;
    if(ok)
        status = mpSession->start().status();
    updateStatus(status);
}

void ScanJob::Private::abortTransfer()
{
    if(atomicTransition(processing, pending)) {
        mStateReason = PWG_NONE;
        mpSession.reset();
    }
}

ScanJob &ScanJob::finishTransfer(std::ostream &os)
{
    p->finishTransfer(os);
    return *this;
}

void ScanJob::Private::finishTransfer(std::ostream &os)
{
    std::shared_ptr<ImageEncoder> pEncoder;
    if(isProcessing()) {
        if(mDocumentFormat == HttpServer::MIME_TYPE_JPEG) {
            auto jpegEncoder = new JpegEncoder;
            jpegEncoder->setQualityPercent(90);
            pEncoder.reset(jpegEncoder);
        }
        else if(mDocumentFormat == HttpServer::MIME_TYPE_PDF) {
            auto pdfEncoder = new PdfEncoder;
            pdfEncoder->documentInfo()["Title"] = mUuid + "/" + sanecpp::dtostr_c(mImagesCompleted);
            pdfEncoder->documentInfo()["Creator"] = mpScanner->makeAndModel() + " (SANE)";
            pdfEncoder->documentInfo()["Producer"] = "AirSane Server";
            pEncoder.reset(pdfEncoder);
        }
        else if(mDocumentFormat == HttpServer::MIME_TYPE_PNG) {
            auto pngEncoder = new PngEncoder;
            pEncoder.reset(pngEncoder);
        }
        else {
            mState = aborted;
            mStateReason = PWG_UNSUPPORTED_DOCUMENT_FORMAT;
        }
    }
    if(isProcessing()) {
        pEncoder->setResolutionDpi(mRes_dpi);
        if(mColorMode == "Color")
            pEncoder->setColorspace(ImageEncoder::RGB);
        else if(mColorMode == "Gray")
            pEncoder->setColorspace(ImageEncoder::Grayscale);
        auto p = mpSession->parameters();
        pEncoder->setWidth(p->pixels_per_line);
        pEncoder->setHeight(p->lines);
        pEncoder->setBitDepth(p->depth);
        pEncoder->setDestination(&os);
        if(pEncoder->bytesPerLine() != p->bytes_per_line) {
            std::cerr << __FILE__ << ", line " << __LINE__
                      << ": encoder bytesPerLine (" << pEncoder->bytesPerLine()
                      << ") differs from SANE bytes_per_line ("
                      << p->bytes_per_line << ")"
                      << std::endl;
            mState = aborted;
            mStateReason = PWG_ERRORS_DETECTED;
        }
    }
    if(isProcessing()) {
        std::vector<char> buffer(mpSession->parameters()->bytes_per_line);
        SANE_Status status = SANE_STATUS_GOOD;
        while(status == SANE_STATUS_GOOD && os && isProcessing()) {
            status = mpSession->read(buffer).status();
            if(status == SANE_STATUS_GOOD) try {
                pEncoder->writeLine(buffer.data());
                if(!os.flush())
                    abortTransfer();
            } catch(const std::runtime_error& e) {
                std::cerr << e.what() << ", aborting" << std::endl;
                mState = aborted;
                mStateReason = PWG_ERRORS_DETECTED;
            }
        }
        if(isProcessing()) {
            ++mImagesCompleted;
            updateStatus(status);
            if(pEncoder->linesLeftInCurrentImage() != pEncoder->height()) {
                std::cerr << "incomplete or excess scan data" << std::endl;
                mState = aborted;
                mStateReason = PWG_ERRORS_DETECTED;
            }
        }
    }
    mpSession.reset();
}

ScanJob& ScanJob::cancel()
{
    if(p->atomicTransition(processing, canceled)
            || p->atomicTransition(pending, canceled))
        p->mStateReason = PWG_JOB_CANCELED_BY_USER;
    return *this;
}

ScanJob::State ScanJob::state() const
{
    return p->mState;
}

std::string ScanJob::statusString() const
{
    return p->statusString();
}

std::string ScanJob::statusReason() const
{
    return std::string(p->mStateReason);
}

bool ScanJob::isPending() const
{
    return p->isPending();
}

bool ScanJob::isProcessing() const
{
    return p->isProcessing();
}

bool ScanJob::isFinished() const
{
    return p->isFinished();
}

bool ScanJob::isAborted() const
{
    return p->isAborted();
}

bool ScanJob::Private::isPending() const
{
    return mState == pending;
}

bool ScanJob::Private::isProcessing() const
{
    return mState == processing;
}

bool ScanJob::Private::isFinished() const
{
    switch(mState) {
    case pending:
    case processing:
        return false;
    }
    return true;
}

bool ScanJob::Private::isAborted() const
{
    return mState == aborted;
}
