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

#include "scanner.h"

#include <sane/saneopts.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <set>
#include <sstream>

#include "basic/uuid.h"
#include "scanjob.h"
#include "web/httpserver.h"

namespace {
std::string
xmlEscape(const std::string& in)
{
  std::string out;
  for (auto c : in)
    switch (c) {
      case '"':
        out += "&quot;";
        break;
      case '\'':
        out += "&apos;";
        break;
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      default:
        out += c;
    }
  return out;
}

std::string
colorMode(const std::string& colorSpace, int bitDepth)
{
  std::ostringstream oss;
  if (colorSpace == "grayscale")
    oss << "Grayscale" << bitDepth;
  else if (colorSpace == "color")
    oss << "RGB" << 3 * bitDepth;
  return oss.str();
}

std::string
findFlatbedName(const std::vector<std::string>& names)
{
  auto i = std::find(names.begin(), names.end(), "Flatbed");
  if (i == names.end())
    i = std::find(names.begin(), names.end(), "FlatBed");
  if (i == names.end())
    i = std::find(names.begin(), names.end(), "Platen");
  if (i == names.end())
    i = std::find(names.begin(), names.end(), "Auto");
  if (i == names.end())
    i = std::find(names.begin(), names.end(), "Normal");
  if (i == names.end())
    return "";
  return *i;
}

std::string
findAdfSimplexName(const std::vector<std::string>& names)
{
  auto i = std::find(names.begin(), names.end(), "Automatic Document Feeder");
  if (i == names.end())
    i = std::find(names.begin(), names.end(), "ADF Simplex");
  if (i == names.end())
    i = std::find(names.begin(), names.end(), "ADF Front");
  if (i == names.end())
    i = std::find(names.begin(), names.end(), "ADF");
  if (i == names.end())
    for (i = names.begin(); i != names.end(); ++i)
      if (i->find("Automatic Document Feeder") != std::string::npos)
        break;
  if (i == names.end())
    return "";
  return *i;
}

std::string
findAdfDuplexName(const std::vector<std::string>& names)
{
  auto i = std::find(names.begin(), names.end(), "ADF Duplex");
  if (i == names.end())
    for (i = names.begin(); i != names.end(); ++i)
      if (i->find("Automatic Document Feeder") != std::string::npos &&
          i->find("Duplex") != std::string::npos)
        break;
  if (i == names.end())
    return "";
  return *i;
}

std::string
findGrayName(const std::vector<std::string>& names)
{
  auto i = std::find(names.begin(), names.end(), "True Gray");
  if (i == names.end())
    i = std::find(names.begin(), names.end(), "Gray");
  if (i == names.end())
    for (i = names.begin(); i != names.end(); ++i)
      if (i->find("Gray") != std::string::npos)
        break;
  if (i == names.end())
    return "";
  return *i;
}

std::string
findColorName(const std::vector<std::string>& names)
{
  auto i = std::find(names.begin(), names.end(), "Color");
  if (i == names.end())
    for (i = names.begin(); i != names.end(); ++i)
      if (i->find("Color") != std::string::npos)
        break;
  if (i == names.end())
    return "";
  return *i;
}

double
roundToNearestStep(double value, double min, double step)
{
  return min + ::floor((value - min) / step) * step;
}

std::vector<double>
discretizeResolutions(double min, double max, double step)
{
  if (step < 1)
    step = 1;
  auto resolutions = std::vector<double>();
  resolutions.push_back(min);
  double r = 75;
  double r2 = roundToNearestStep(r, min, step);
  while (r2 <= max) {
    if (r2 > resolutions.back())
      resolutions.push_back(r2);
    r *= 2;
    r2 = roundToNearestStep(r, min, step);
  }
  r2 = roundToNearestStep(max, min, step);
  if (r2 > resolutions.back())
    resolutions.push_back(r2);
  return resolutions;
}

} // namespace

struct Scanner::Private
{
  static std::set<Scanner::Private*> sInstances;

  Scanner* p;
  
  sanecpp::device_info mDeviceInfo;
  std::string mMakeAndModel, mStableUniqueName, mUuid, mPublishedName, mUri;
  std::string mAdminUrl, mIconUrl;

  int mMinResDpi, mMaxResDpi, mResStepDpi;
  double mMaxWidthPx300dpi, mMaxHeightPx300dpi;
  std::vector<double> mDiscreteResolutions;
  std::vector<std::string> mDocumentFormats, mTxtColorSpaces, mColorModes,
    mInputSources;

  struct InputSource
  {
    Private* p;
    std::string mSourceName;
    std::vector<std::string> mSupportedIntents;
    double mMinWidth, mMaxWidth, mMinHeight, mMaxHeight, mMaxPhysicalWidth,
      mMaxPhysicalHeight;
    int mMaxBits;

    explicit InputSource(Private* p);
    const char* init(const sanecpp::option_set&);
    void writeCapabilitiesXml(std::ostream&) const;
  } * mpPlaten, *mpAdfSimplex, *mpAdfDuplex;

  std::string mGrayScanModeName, mColorScanModeName;
  mutable int mCurrentProfile;
  OptionsFile::Options mDeviceOptions;

  std::map<std::string, std::shared_ptr<ScanJob>> mJobs;
  std::mutex mJobsMutex;

  std::weak_ptr<sanecpp::session> mpSession;

  SANE_Status mTemporaryAdfStatus;

  const char* mError;

  Private(Scanner*);
  ~Private();
  void init(const sanecpp::device_info&);
  const char* init2(const OptionsFile&);
  void generateStableUniqueName();
  void writeScannerCapabilitiesXml(std::ostream&) const;
  void writeSettingProfile(int bits, std::ostream&) const;
  std::shared_ptr<ScanJob> createJob();
  bool isOpen() const;
  const char* statusString() const;
  const char* temporaryAdfStatusString();
};

std::set<Scanner::Private*> Scanner::Private::sInstances;

Scanner::Private::Private(Scanner* p)
  : p(p)
  , mpPlaten(nullptr)
  , mpAdfSimplex(nullptr)
  , mpAdfDuplex(nullptr)
  , mTemporaryAdfStatus(SANE_STATUS_GOOD)
  , mError(nullptr)
{
  sInstances.insert(this);
}

Scanner::Private::~Private()
{
  delete mpPlaten;
  delete mpAdfSimplex;
  delete mpAdfDuplex;
  sInstances.erase(this);
}

void
Scanner::Private::writeScannerCapabilitiesXml(std::ostream& os) const
{
  mCurrentProfile = 0;
  os << "<?xml version='1.0' encoding='UTF-8'?>\r\n"
        "<scan:ScannerCapabilities"
        " xmlns:pwg='http://www.pwg.org/schemas/2010/12/sm'"
        " xmlns:scan='http://schemas.hp.com/imaging/escl/2011/05/03'>\r\n"
        "<pwg:Version>2.0</pwg:Version>\r\n"
        "<pwg:MakeAndModel>"
     << xmlEscape(mMakeAndModel)
     << "</pwg:MakeAndModel>\r\n"
     << "<pwg:SerialNumber>1.0</pwg:SerialNumber>\r\n"
     << "<scan:UUID>"
     << mUuid << "</scan:UUID>\r\n";
  if (!mAdminUrl.empty())
    os << "<scan:AdminURI>" << mAdminUrl << "</scan:AdminURI>\r\n";
  if (!mIconUrl.empty())
    os << "<scan:IconURI>" << mIconUrl << "</scan:IconURI>\r\n";
  if (mpPlaten) {
    os << "<scan:Platen>\r\n<scan:PlatenInputCaps>\r\n";
    mpPlaten->writeCapabilitiesXml(os);
    os << "</scan:PlatenInputCaps>\r\n</scan:Platen>\r\n";
  }
  if (mpAdfSimplex) {
    os << "<scan:Adf>\r\n<scan:AdfSimplexInputCaps>\r\n";
    mpAdfSimplex->writeCapabilitiesXml(os);
    os << "</scan:AdfSimplexInputCaps>\r\n";
    if (mpAdfDuplex) {
      os << "<scan:AdfDuplexInputCaps>\r\n";
      mpAdfDuplex->writeCapabilitiesXml(os);
      os << "</scan:AdfDuplexInputCaps>\r\n";
    }
    os << "<scan:AdfOptions>\r\n"
       << "<scan:AdfOption>DetectPaperLoaded</scan:AdfOption>\r\n";
    if (mpAdfDuplex) {
      os << "<scan:AdfOption>Duplex</scan:AdfOption>\r\n";
    }
    os << "</scan:AdfOptions>\r\n"
       << "</scan:Adf>\r\n";
  }
  os << "</scan:ScannerCapabilities>\r\n";
}

void
Scanner::Private::writeSettingProfile(int bits, std::ostream& os) const
{
  os << "<scan:SettingProfile name='" << mCurrentProfile++
     << "'>\r\n"
        "<scan:ColorModes>\r\n";
  for (const auto& cs : mTxtColorSpaces)
    for (int i = 8; i <= bits; i += 8)
      os << "<scan:ColorMode>" << colorMode(cs, i) << "</scan:ColorMode>\r\n";
  os << "</scan:ColorModes>\r\n"
        "<scan:ColorSpaces>\r\n";
  os << "<scan:ColorSpace>"
     << "RGB"
     << "</scan:ColorSpace>\r\n";
  os << "</scan:ColorSpaces>\r\n"
        "<scan:SupportedResolutions>\r\n";
  if (mDiscreteResolutions.empty()) {
    os << "<scan:ResolutionRange />\r\n"
       << "<scan:XResolutionRange>\r\n"
          "<scan:Min>"
       << mMinResDpi
       << "</scan:Min>\r\n"
          "<scan:Max>"
       << mMaxResDpi
       << "</scan:Max>\r\n"
          "<scan:Step>"
       << mResStepDpi
       << "</scan:Step>\r\n"
          "</scan:XResolutionRange>\r\n"
          "<scan:YResolutionRange>\r\n"
          "<scan:Min>"
       << mMinResDpi
       << "</scan:Min>\r\n"
          "<scan:Max>"
       << mMaxResDpi
       << "</scan:Max>\r\n"
          "<scan:Step>"
       << mResStepDpi
       << "</scan:Step>\r\n"
          "</scan:YResolutionRange>\r\n";
  } else {
    os << "<scan:DiscreteResolutions>\r\n";
    for (const auto& res : mDiscreteResolutions)
      os << "<scan:DiscreteResolution>\r\n"
            "<scan:XResolution>"
         << res
         << "</scan:XResolution>\r\n"
            "<scan:YResolution>"
         << res
         << "</scan:YResolution>\r\n"
            "</scan:DiscreteResolution>\r\n";
    os << "</scan:DiscreteResolutions>\r\n";
  }
  os << "</scan:SupportedResolutions>\r\n"
        "<scan:DocumentFormats>\r\n";
  for (const auto& format : mDocumentFormats)
    os << "<pwg:DocumentFormat>" << format << "</pwg:DocumentFormat>\r\n";
  os << "</scan:DocumentFormats>\r\n"
        "</scan:SettingProfile>\r\n";
}

std::shared_ptr<ScanJob>
Scanner::Private::createJob()
{
  std::lock_guard<std::mutex> lock(mJobsMutex);
  std::string jobUuid;
  do {
    jobUuid = Uuid(mUuid, ::time(nullptr), ::rand()).toString();
  } while (mJobs.find(jobUuid) != mJobs.end());
  auto job = std::make_shared<ScanJob>(p, jobUuid);
  mJobs[jobUuid] = job;
  return job;
}

bool
Scanner::Private::isOpen() const
{
  return !!mpSession.lock();
}

const char*
Scanner::Private::statusString() const
{
  return isOpen() ? "Processing" : "Idle";
}

const char*
Scanner::Private::temporaryAdfStatusString()
{
  SANE_Status adfStatus = mTemporaryAdfStatus;
  mTemporaryAdfStatus = SANE_STATUS_GOOD;
  switch (adfStatus) {
    case SANE_STATUS_GOOD:
      return "ScannerAdfLoaded";
    case SANE_STATUS_JAMMED:
      return "ScannerAdfJam";
    case SANE_STATUS_COVER_OPEN:
      return "ScannerAdfDoorOpen";
    case SANE_STATUS_NO_DOCS:
      return "ScannerAdfEmpty";
    default:
      break;
  }
  return "";
}

Scanner::Private::InputSource::InputSource(Private* p)
  : p(p)
{}

void
Scanner::Private::InputSource::writeCapabilitiesXml(std::ostream& os) const
{
  os << "<scan:MinWidth>" << mMinWidth
     << "</scan:MinWidth>\r\n"
        "<scan:MinHeight>"
     << mMinHeight
     << "</scan:MinHeight>\r\n"
        "<scan:MaxWidth>"
     << mMaxWidth
     << "</scan:MaxWidth>\r\n"
        "<scan:MaxHeight>"
     << mMaxHeight
     << "</scan:MaxHeight>\r\n"
        "<scan:MaxPhysicalWidth>"
     << mMaxPhysicalWidth
     << "</scan:MaxPhysicalWidth>\r\n"
        "<scan:MaxPhysicalHeight>"
     << mMaxPhysicalHeight
     << "</scan:MaxPhysicalHeight>\r\n"
        "<scan:MaxScanRegions>1</scan:MaxScanRegions>\r\n"
        "<scan:SettingProfiles>\r\n";
  p->writeSettingProfile(mMaxBits, os);
  os << "</scan:SettingProfiles>\r\n"
        "<scan:SupportedIntents>\r\n";
  for (const auto& s : mSupportedIntents)
    os << "<scan:SupportedIntent>" << s << "</scan:SupportedIntent>\r\n";
  os << "</scan:SupportedIntents>\r\n";
}

void
Scanner::Private::generateStableUniqueName()
{
  // We construct a name that is stable with regard to USB renumbering.
  std::string s;
  size_t pos = mDeviceInfo.name.find(':');
  if (pos == std::string::npos)
    s = mDeviceInfo.name + ':';
  else
    s = mDeviceInfo.name.substr(0, pos + 1);
  s += mMakeAndModel + ':';
  int i = 0;
  std::ostringstream oss;
  bool found = false;
  do {
    oss.str("");
    oss << s << ++i;
    found = false;
    for (auto p : sInstances)
      if (p != this && p->mStableUniqueName == oss.str())
        found = true;
  } while (found);
  mStableUniqueName = oss.str();
}

void
Scanner::Private::init(const sanecpp::device_info& info)
{
  mDeviceInfo = info;
  mMakeAndModel = info.vendor + " " + info.model;
  mPublishedName = mMakeAndModel;
  generateStableUniqueName();
  mUuid = Uuid(mStableUniqueName).toString();
}

const char*
Scanner::Private::init2(const OptionsFile& optionsfile)
{
  auto device = sanecpp::open(mDeviceInfo);
  if (!device)
    return "failed to open device";

  sanecpp::option_set opt(device);
  // Apply device options first so any changes to dependent parameters
  // are detected during initialization.
  mDeviceOptions = optionsfile.scannerOptions(p);
  for (const auto &option : mDeviceOptions.sane_options) {
    if (opt[option.first].is_null()) {
      std::clog << "SANE option specified in options file: " << option.first << ", does not exist" << std::endl;
    }
    else {
      std::clog << "applying SANE option " << option.first << ":=" << option.second << std::endl;
      opt[option.first] = option.second;
    }
  }

  const auto& resolution = opt[SANE_NAME_SCAN_RESOLUTION];
  if (resolution.is_null())
    return "missing SANE parameter: " SANE_NAME_SCAN_RESOLUTION;
  mMinResDpi = resolution.min();
  mMaxResDpi = resolution.max();
  mResStepDpi = resolution.quant();
  mDiscreteResolutions = resolution.allowed_numeric_values();
  if (mDiscreteResolutions
        .empty()) // mopria client assumes discrete resolutions
    mDiscreteResolutions =
      discretizeResolutions(mMinResDpi, mMaxResDpi, mResStepDpi);

  mDocumentFormats = std::vector<std::string>({
    HttpServer::MIME_TYPE_PDF,
    HttpServer::MIME_TYPE_JPEG,
    HttpServer::MIME_TYPE_PNG,
  });

  auto modes = opt[SANE_NAME_SCAN_MODE].allowed_string_values();
  if (modes.empty()) {
    modes.push_back("Gray");
    modes.push_back("Color");
  }
  mGrayScanModeName = findGrayName(modes);
  mColorScanModeName = findColorName(modes);
  if (mGrayScanModeName.empty() && mColorScanModeName.empty()) {
    mGrayScanModeName = "Gray"; // make sure we have at least one scan mode
  }
  if (!mGrayScanModeName.empty()) {
    mTxtColorSpaces.push_back("grayscale");
    mColorModes.push_back("Grayscale8");
  }
  if (!mColorScanModeName.empty()) {
    mTxtColorSpaces.push_back("color");
    mColorModes.push_back("RGB24");
  }

  const char* err = nullptr;
  int maxBits = 8;
  mMaxWidthPx300dpi = 0;
  mMaxHeightPx300dpi = 0;

  auto sources = opt[SANE_NAME_SCAN_SOURCE].allowed_string_values();
  auto flatbedName = findFlatbedName(sources),
       adfSimplexName = findAdfSimplexName(sources),
       adfDuplexName = findAdfDuplexName(sources), adfName = std::string();
  if (!adfDuplexName.empty())
    adfName = adfDuplexName;
  else if (!adfSimplexName.empty())
    adfName = adfSimplexName;
  if (adfName.empty() && flatbedName.empty())
    flatbedName = "-";

  if (!flatbedName.empty()) {
    mInputSources.push_back("Platen");
    if (flatbedName != "-")
      opt[SANE_NAME_SCAN_SOURCE].set_string_value(flatbedName);
    mpPlaten = new Private::InputSource(this);
    err = mpPlaten->init(opt);
    if (!err) {
      mpPlaten->mSupportedIntents = std::vector<std::string>({
        "Preview",
        "TextAndGraphic",
        "Photo",
      });
      maxBits = std::max(maxBits, mpPlaten->mMaxBits);
      mMaxWidthPx300dpi = std::max(mMaxWidthPx300dpi, mpPlaten->mMaxWidth);
      mMaxHeightPx300dpi = std::max(mMaxHeightPx300dpi, mpPlaten->mMaxHeight);
    }
  }
  if (!adfSimplexName.empty() || !adfDuplexName.empty()) {
      mInputSources.push_back("Feeder");
  }
  if (!adfSimplexName.empty()) {
    opt[SANE_NAME_SCAN_SOURCE].set_string_value(adfSimplexName);
    mpAdfSimplex = new Private::InputSource(this);
    err = mpAdfSimplex->init(opt);
    if (!err) {
      mpAdfSimplex->mSupportedIntents = std::vector<std::string>({
        "TextAndGraphic",
        "Photo",
      });
      maxBits = std::max(maxBits, mpAdfSimplex->mMaxBits);
      mMaxWidthPx300dpi = std::max(mMaxWidthPx300dpi, mpAdfSimplex->mMaxWidth);
      mMaxHeightPx300dpi = std::max(mMaxHeightPx300dpi, mpAdfSimplex->mMaxHeight);
    }
  }
  if (!adfDuplexName.empty()) {
    opt[SANE_NAME_SCAN_SOURCE].set_string_value(adfDuplexName);
    mpAdfDuplex = new Private::InputSource(this);
    err = mpAdfDuplex->init(opt);
    if (!err) {
      mpAdfDuplex->mSupportedIntents = std::vector<std::string>({
        "TextAndGraphic",
        "Photo",
      });
      maxBits = std::max(maxBits, mpAdfDuplex->mMaxBits);
      mMaxWidthPx300dpi = std::max(mMaxWidthPx300dpi, mpAdfDuplex->mMaxWidth);
      mMaxHeightPx300dpi = std::max(mMaxHeightPx300dpi, mpAdfDuplex->mMaxHeight);
    }
  }
  if (maxBits == 16) {
    if (std::find(mColorModes.begin(), mColorModes.end(), "Grayscale8") !=
        mColorModes.end())
      mColorModes.push_back("Grayscale16");
    if (std::find(mColorModes.begin(), mColorModes.end(), "RGB24") !=
        mColorModes.end())
      mColorModes.push_back("RGB48");
  }
  return err;
}

const char*
Scanner::Private::InputSource::init(const sanecpp::option_set& opt)
{
  mSourceName = opt[SANE_NAME_SCAN_SOURCE].string_value();

  mMaxBits = 8;
  if (!opt[SANE_NAME_BIT_DEPTH].is_null())
    mMaxBits = opt[SANE_NAME_BIT_DEPTH].max();

  // Defaults in case TL_X etc are not defined.
  // SANE requests that backends must work in the absence of those options.
  // We define a max height and width that allows for both US Letter and A4.
  mMinWidth = 0;
  mMaxWidth = 216; // US Letter width in mm
  mMinHeight = 0;
  mMaxHeight = 297; // A4 height in mm
  mMaxPhysicalWidth = mMaxWidth;
  mMaxPhysicalHeight = mMaxHeight;
  SANE_Unit unit = SANE_UNIT_MM;

  const auto &tl_x = opt[SANE_NAME_SCAN_TL_X], &tl_y = opt[SANE_NAME_SCAN_TL_Y],
             &br_x = opt[SANE_NAME_SCAN_BR_X], &br_y = opt[SANE_NAME_SCAN_BR_Y];

  if (!tl_x.is_null() && !tl_y.is_null() && !br_x.is_null() && !br_y.is_null())
  {
    unit = tl_x.unit();
    if (tl_y.unit() != unit || br_x.unit() != unit || br_y.unit() != unit)
      return "inconsistent unit in scan area parameters";

    mMinWidth = std::max(0.0, br_x.min() - tl_x.max());
    mMaxWidth = br_x.max() - tl_x.min();
    mMaxPhysicalWidth = br_x.max();
    mMinHeight = std::max(0.0, br_y.min() - tl_y.max());
    mMaxHeight = br_y.max() - tl_y.min();
    mMaxPhysicalHeight = br_y.max();
  }

  // eSCL expresses sizes in terms of pixels at 300 dpi
  double f = 300;
  switch (unit) {
    case SANE_UNIT_MM:
      f /= 25.4;
      break;
    case SANE_UNIT_PIXEL:
      f /= opt[SANE_NAME_SCAN_RESOLUTION].numeric_value();
      break;
    default:
      return "unexpected unit in scan area parameters";
  }

  for (auto pValue : { &mMinWidth,
                       &mMaxWidth,
                       &mMinHeight,
                       &mMaxHeight,
                       &mMaxPhysicalWidth,
                       &mMaxPhysicalHeight })
    *pValue = ::floor(*pValue * f + 0.5);
  return nullptr;
}

Scanner::Scanner(const sanecpp::device_info& info)
  : p(new Private(this))
{
  p->init(info);
}

bool
Scanner::initWithOptions(const OptionsFile& optionsfile)
{
  p->mError = p->init2(optionsfile);
  return p->mError == nullptr;
}

Scanner::~Scanner()
{
  delete p;
}

const char*
Scanner::error() const
{
  return p->mError;
}

std::string
Scanner::statusString() const
{
  return p->statusString();
}

void
Scanner::setTemporaryAdfStatus(SANE_Status status)
{
  p->mTemporaryAdfStatus = status;
}

const std::string&
Scanner::uuid() const
{
  return p->mUuid;
}

const std::string&
Scanner::makeAndModel() const
{
  return p->mMakeAndModel;
}

const std::string&
Scanner::saneName() const
{
  return p->mDeviceInfo.name;
}

const std::string&
Scanner::stableUniqueName() const
{
  return p->mStableUniqueName;
}

const std::string&
Scanner::publishedName() const
{
  return p->mPublishedName;
}

void
Scanner::setPublishedName(const std::string& name)
{
  p->mPublishedName = name;
}

void
Scanner::setUri(const std::string& uri)
{
  p->mUri = uri;
}

const std::string&
Scanner::uri() const
{
  return p->mUri;
}

void
Scanner::setAdminUrl(const std::string& url)
{
  p->mAdminUrl = url;
}

const std::string&
Scanner::adminUrl() const
{
  return p->mAdminUrl;
}

const std::string&
Scanner::iconFile() const
{
  return p->mDeviceOptions.icon;
}

const std::string&
Scanner::note() const
{
  return p->mDeviceOptions.note;
}

void
Scanner::setIconUrl(const std::string& url)
{
  p->mIconUrl = url;
}

const std::string&
Scanner::iconUrl() const
{
  return p->mIconUrl;
}

const std::vector<std::string>&
Scanner::documentFormats() const
{
  return p->mDocumentFormats;
}

const std::vector<std::string>&
Scanner::txtColorSpaces() const
{
  return p->mTxtColorSpaces;
}

const std::vector<std::string>&
Scanner::colorModes() const
{
  return p->mColorModes;
}

std::vector<std::string>
Scanner::platenSupportedIntents() const
{
  return p->mpPlaten ? p->mpPlaten->mSupportedIntents
                     : std::vector<std::string>();
  ;
}

std::vector<std::string>
Scanner::adfSimplexSupportedIntents() const
{
  return p->mpAdfSimplex ? p->mpAdfSimplex->mSupportedIntents : std::vector<std::string>();
  ;
}

std::vector<std::string>
Scanner::adfDuplexSupportedIntents() const
{
  return p->mpAdfDuplex ? p->mpAdfDuplex->mSupportedIntents : std::vector<std::string>();
  ;
}

const std::vector<std::string>&
Scanner::inputSources() const
{
  return p->mInputSources;
}

int
Scanner::minResDpi() const
{
  return p->mMinResDpi;
}

int
Scanner::maxResDpi() const
{
  return p->mMaxResDpi;
}

int
Scanner::maxWidthPx300dpi() const
{
  return p->mMaxWidthPx300dpi;
}

int
Scanner::maxHeightPx300dpi() const
{
  return p->mMaxHeightPx300dpi;
}

bool
Scanner::hasPlaten() const
{
  return p->mpPlaten;
}

bool
Scanner::hasAdf() const
{
  return p->mpAdfSimplex;
}

bool
Scanner::hasDuplexAdf() const
{
  return p->mpAdfDuplex;
}

std::string
Scanner::platenSourceName() const
{
  return p->mpPlaten ? p->mpPlaten->mSourceName : "";
}

std::string
Scanner::adfSimplexSourceName() const
{
  return p->mpAdfSimplex ? p->mpAdfSimplex->mSourceName : "";
}

std::string
Scanner::adfDuplexSourceName() const
{
  return p->mpAdfDuplex ? p->mpAdfDuplex->mSourceName : "";
}

std::string
Scanner::grayScanModeName() const
{
  return p->mGrayScanModeName;
}

std::string
Scanner::colorScanModeName() const
{
  return p->mColorScanModeName;
}

std::shared_ptr<sanecpp::session>
Scanner::open()
{
  auto session = std::make_shared<sanecpp::session>(p->mDeviceInfo.name);
  p->mpSession = session;
  return session;
}

bool
Scanner::isOpen() const
{
  return p->isOpen();
}

void
Scanner::writeScannerCapabilitiesXml(std::ostream& os) const
{
  os.imbue(std::locale("C"));
  p->writeScannerCapabilitiesXml(os);
}

std::shared_ptr<ScanJob>
Scanner::createJobFromScanSettingsXml(const std::string& xml,
                                      bool autoselectFormat)
{
  auto job = p->createJob();
  job->initWithScanSettingsXml(xml, autoselectFormat, p->mDeviceOptions);
  return job;
}

std::shared_ptr<ScanJob>
Scanner::getJob(const std::string& uuid)
{
  std::lock_guard<std::mutex> lock(p->mJobsMutex);
  auto i = p->mJobs.find(uuid);
  return i == p->mJobs.end() ? nullptr : i->second;
}

bool
Scanner::cancelJob(const std::string& uuid)
{
  std::lock_guard<std::mutex> lock(p->mJobsMutex);
  auto i = p->mJobs.find(uuid);
  if (i == p->mJobs.end())
    return false;
  i->second->cancel();
  return true;
}

int
Scanner::purgeJobs(int maxIdleSeconds)
{
  int n = 0;
  std::lock_guard<std::mutex> lock(p->mJobsMutex);
  for (auto i = p->mJobs.begin(); i != p->mJobs.end();) {
    if (i->second->idleSeconds() > maxIdleSeconds) {
      i = p->mJobs.erase(i);
      ++n;
    } else {
      ++i;
    }
  }
  return n;
}

Scanner::JobList
Scanner::jobs() const
{
  JobList jobs;
  std::lock_guard<std::mutex> lock(p->mJobsMutex);
  for (const auto& job : p->mJobs)
    jobs.push_back(job.second);
  return jobs;
}

void
Scanner::writeScannerStatusXml(std::ostream& os) const
{
  os << "<?xml version='1.0' encoding='UTF-8'?>\r\n"
        "<scan:ScannerStatus xmlns:pwg='http://www.pwg.org/schemas/2010/12/sm'"
        " xmlns:scan='http://schemas.hp.com/imaging/escl/2011/05/03'>\r\n"
        "<pwg:Version>2.0</pwg:Version>\r\n"
        "<pwg:State>"
     << p->statusString()
     << "</pwg:State>\r\n";

  if (p->mpAdfSimplex || p->mpAdfDuplex)
    os << "<scan:AdfState>" << p->temporaryAdfStatusString()
       << "</scan:AdfState>\r\n";

  os << "<scan:Jobs>\r\n";
  std::lock_guard<std::mutex> lock(p->mJobsMutex);
  for (const auto& job : p->mJobs)
    job.second->writeJobInfoXml(os);

  os << "</scan:Jobs>\r\n</scan:ScannerStatus>\r\n" << std::flush;
}
