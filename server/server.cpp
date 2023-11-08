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

#include "server.h"

#include <cstring>
#include <cmath>
#include <csignal>
#include <ctime>
#include <cstdint>
#include <regex>
#include <sstream>
#include <iomanip>
#include <unistd.h>

#include "mainpage.h"
#include "scannerpage.h"
#include "optionsfile.h"
#include "scanjob.h"
#include "scanner.h"
#include "purgethread.h"
#include "basic/uuid.h"
#include "zeroconf/hotplugnotifier.h"
#include "zeroconf/networkhotplugnotifier.h"
#include "web/accessfile.h"

extern const char* GIT_COMMIT_HASH;
extern const char* GIT_BRANCH;
extern const char* GIT_REVISION_NUMBER;
extern const char* BUILD_TIME_STAMP;

namespace {

std::string hostname()
{
  char buf[256];
  ::gethostname(buf, sizeof(buf));
  return buf;
}

struct Notifier : HotplugNotifier
{
  Server& server;
  explicit Notifier(Server& s)
    : server(s)
  {}
  void onHotplugEvent(Event ev) override
  {
    switch (ev) {
      case deviceArrived:
      case deviceLeft:
        std::clog << "hotplug event, reloading configuration" << std::endl;
        server.terminate(SIGHUP);
        break;
      case other:
        break;
    }
  }
};

struct NetworkNotifier : NetworkHotplugNotifier
{
  Server& server;
  explicit NetworkNotifier(Server& s)
    : server(s)
  {}
  void onHotplugEvent(Event ev) override
  {
    switch (ev) {
      case addressArrived:
      case addressLeft:
        std::clog << "network hotplug event, reloading configuration" << std::endl;
        server.terminate(SIGHUP);
        break;
      case other:
        break;
    }
  }
};

bool
clientIsAirscan(const HttpServer::Request& req)
{
  return req.header(HttpServer::HTTP_HEADER_USER_AGENT)
           .find("AirScanScanner") != std::string::npos;
}

} // namespace

Server::Server(int argc, char** argv)
  : mAnnounce(true)
  , mWebinterface(true)
  , mResetoption(false)
  , mDiscloseversion(true)
  , mLocalonly(true)
  , mHotplug(true)
  , mNetworkhotplug(true)
  , mRandompaths(false)
  , mCompatiblepath(false)
  , mJobtimeout(0)
  , mPurgeinterval(0)
  , mStartupTimeSeconds(0)
  , mDoRun(true)
{
  std::string port, interface, unixsocket, accesslog, hotplug, networkhotplug,
     announce, webinterface, resetoption, discloseversion, localonly, optionsfile,
     ignorelist, accessfile, randompaths, compatiblepath, debug, announcesecure,
     jobtimeout, purgeinterval;
  struct
  {
    const std::string name, def, info;
    std::string& value;
  } options[] = {
    { "listen-port", "8090", "listening port", port },
    { "interface", "", "listen on named interface only", interface },
    { "unix-socket", "", "listen on named unix socket", unixsocket },
    { "access-log", "", "HTTP access log, - for stdout", accesslog },
    { "hotplug", "true", "repeat scanner search on hotplug event", hotplug },
    { "network-hotplug", "true", "restart server on network change", networkhotplug },
    { "mdns-announce", "true", "announce scanners via mDNS", announce },
    { "announce-secure", "false", "announce secure connection", announcesecure },
    { "web-interface", "true", "enable web interface", webinterface },
    { "reset-option", "false", "allow server reset from web interface", resetoption },
    { "disclose-version", "true", "disclose version information in web interface", discloseversion },
    { "random-paths", "false", "prepend a random uuid to scanner paths", randompaths },
    { "compatible-path", "true", "use /eSCL as path for first scanner", compatiblepath },
    { "local-scanners-only", "false", "ignore SANE network scanners", localonly },
    { "job-timeout", "120", "timeout for idle jobs (seconds)", jobtimeout },
    { "purge-interval", "5", "how often job lists are purged (seconds)", purgeinterval },
    { "options-file",
#ifdef __FreeBSD__
      "/usr/local/etc/airsane/options.conf",
#else
      "/etc/airsane/options.conf",
#endif
      "location of device options file",
      optionsfile },
    { "ignore-list",
#ifdef __FreeBSD__
      "/usr/local/etc/airsane/ignore.conf",
#else
      "/etc/airsane/ignore.conf",
#endif
      "location of device ignore list",
      ignorelist },
    { "access-file",
#ifdef __FreeBSD__
      "/usr/local/etc/airsane/access.conf",
#else
      "/etc/airsane/access.conf",
#endif
      "location of access file",
      accessfile },
    { "debug", "false", "log debug information to stderr", debug },
  };
  for (auto& opt : options)
    opt.value = opt.def;
  bool help = false;
  for (int i = 1; i < argc; ++i) {
    std::string option = argv[i];
    bool found = false;
    for (auto& opt : options) {
      if (option.rfind("--" + opt.name + "=", 0) == 0) {
        found = true;
        opt.value = option.substr(option.find('=') + 1);
        break;
      } else if (option == "--" + opt.name) {
        found = true;
        help = true;
        std::cerr << "missing argument for option " << option << std::endl;
        break;
      }
    }
    if (!found) {
      help = true;
      if (option != "--help")
        std::cerr << "unknown option: " << option << std::endl;
    }
  }

  if (debug != "true")
    std::clog.rdbuf(nullptr);
  sanecpp::log.rdbuf(std::clog.rdbuf());

  mHotplug = (hotplug == "true");
  mNetworkhotplug = (networkhotplug == "true");
  mAnnounce = (announce == "true");
  mAnnouncesecure = (announcesecure == "true");
  mWebinterface = (webinterface == "true");
  mResetoption = (resetoption == "true");
  mRandompaths = (randompaths == "true");
  mCompatiblepath = (compatiblepath == "true");
  mDiscloseversion = (discloseversion == "true");
  mLocalonly = (localonly == "true");
  mOptionsfile = optionsfile;
  mAccessfile = accessfile;
  mIgnorelist = ignorelist;

  uint16_t port_ = 0;
  if (!(std::istringstream(port) >> port_)) {
    std::cerr << "invalid port number: " << port << std::endl;
    mDoRun = false;
  }
  if (!(std::istringstream(jobtimeout) >> mJobtimeout) || mJobtimeout < 1) {
    std::cerr << "invalid job timeout: " << mJobtimeout << std::endl;
    mDoRun = false;
  }
  if (!(std::istringstream(purgeinterval) >> mPurgeinterval) || mPurgeinterval < 1) {
    std::cerr << "invalid purge interval: " << mPurgeinterval << std::endl;
    mDoRun = false;
  }
  if (mJobtimeout <= mPurgeinterval) {
    std::cerr << "job timeout must be greater than purge interval" << std::endl;
  }
  if (help) {
    std::cout << "options, and their defaults, are:\n";
    for (auto& opt : options)
      std::cout << " --" << opt.name << "=" << opt.def << "\t" << opt.info
                << "\n";
    std::cout << " --help\t"
              << "show this help" << std::endl;
    mDoRun = false;
  }
  if (mDoRun) {
    if (!interface.empty())
      setInterfaceName(interface);
    setPort(port_);
    setUnixSocket(unixsocket);
    if (accesslog.empty())
      std::cout.rdbuf(nullptr);
    else if (accesslog != "-")
      std::cout.rdbuf(mLogfile.open(accesslog, std::ios::app));

    std::clog << "git commit: " << GIT_COMMIT_HASH << " (branch " << GIT_BRANCH
              << ", rev " << GIT_REVISION_NUMBER << ")\n"
              << "build date: " << BUILD_TIME_STAMP << std::endl;
  }
}

Server::~Server() {}

bool
Server::run()
{
  if (!mDoRun)
    return false;

  std::shared_ptr<Notifier> pNotifier;
  if (mHotplug)
    pNotifier = std::make_shared<Notifier>(*this);

  std::shared_ptr<NetworkNotifier> pNetworkNotifier;
  if (mNetworkhotplug)
    pNetworkNotifier = std::make_shared<NetworkNotifier>(*this);

  bool ok = false, done = false;
  do {
    if (unixSocket().empty()) {
      AccessFile accessfile(mAccessfile);
      if (!accessfile.errors().empty()) {
        std::clog << "errors in accessfile:\n" << accessfile.errors() << " terminating" << std::endl;
        return false;
      }
      HttpServer::applyAccessFile(accessfile);
    }

    struct timespec t = { 0 };
    ::clock_gettime(CLOCK_MONOTONIC, &t);
    float t0 = 1.0 * t.tv_sec + 1e-9 * t.tv_nsec;
    std::clog << "start time is " << std::fixed << std::setprecision(2) << t0 << std::endl;

    OptionsFile optionsfile(mOptionsfile);
    std::clog << "enumerating " << (mLocalonly ? "local " : " ") << "devices..."
              << std::endl;
    std::string pathPrefix = "/";
    if (mRandompaths)
      pathPrefix += Uuid::Random().toString() + "/";
    auto scanners = sanecpp::enumerate_devices(mLocalonly);
    int scannerCount = 0;
    for (const auto& s : scanners) {
      std::clog << "found: " << s.name << " (" << s.vendor << " " << s.model
                << ")" << std::endl;
      if (matchIgnorelist(s)) {
        std::clog << "ignoring " << s.name << std::endl;
        continue;
      }
      auto pScanner = std::make_shared<Scanner>(s);
      std::clog << "stable unique name: " << pScanner->stableUniqueName()
                << std::endl;
      std::clog << "uuid: " << pScanner->uuid() << std::endl;

      chooseUniquePublishedName(pScanner.get());

      if (!pScanner->initWithOptions(optionsfile)) {
        std::clog << "error: " << pScanner->error() << std::endl;
      }
      else {
        if (scannerCount++ == 0 && mCompatiblepath)
            pScanner->setUri("/eSCL");
        else
            pScanner->setUri(pathPrefix + pScanner->uuid());
        std::ostringstream url;
        url << "http";
        if (mAnnouncesecure)
          url << "s";
        url << "://" << hostname() << ":" << port()
            << pScanner->uri();
        if (mWebinterface)
          pScanner->setAdminUrl(url.str());
        if (!pScanner->iconFile().empty()) {
          url << "/ScannerIcon";
          pScanner->setIconUrl(url.str());
        }

        std::shared_ptr<MdnsPublisher::Service> pService;
        if (mAnnounce && !pScanner->error()) {
          pService = buildMdnsService(pScanner.get());
          pService->setPort(port());
          if (pService->announce()) {
            std::clog << "published as '" << pService->name() << "'" << std::endl;
              // may have changed due to collision
            pScanner->setPublishedName(pService->name());
          } else
            pService.reset();
          }
          if (pService && !pScanner->error())
            mScanners.push_back(ScannerEntry({ pScanner, pService }));
      }
    }

    ::clock_gettime(CLOCK_MONOTONIC, &t);
    float t1 = 1.0 * t.tv_sec + 1e-9 * t.tv_nsec;
    std::clog << "end time is " << t1 << std::endl;
    mStartupTimeSeconds = t1 - t0;
    std::clog << "startup took " << mStartupTimeSeconds << " secconds" << std::endl;

    {
      PurgeThread purgethread(mScanners, mPurgeinterval, mJobtimeout);
      ok = HttpServer::run();
    }
    mScanners.clear();
    if (ok && terminationStatus() == SIGHUP) {
      std::clog << "received SIGHUP, reloading" << std::endl;
    } else if (ok && terminationStatus() == SIGTERM) {
      std::clog << "received SIGTERM, exiting" << std::endl;
      done = true;
    } else {
      ok = false;
      done = true;
    }
  } while (!done);
  if (ok) {
    std::clog << "server finished ok" << std::endl;
  } else {
    std::cerr << "server finished with error status " << terminationStatus()
              << ", last error was " << lastError() << ": "
              << ::strerror(lastError()) << std::endl;
  }
  return ok;
}

void
Server::chooseUniquePublishedName(Scanner* pScanner) const
{
  std::string baseName = pScanner->publishedName(),
              ext = "";
  int i = 1;
  while (publishedNameExists(baseName + ext))
    ext = " (" + std::to_string(++i) + ")";
  pScanner->setPublishedName(baseName + ext);
}

bool
Server::publishedNameExists(const std::string& name) const
{
  for (const auto& entry : mScanners)
    if (entry.pScanner->publishedName() == name)
      return true;
  return false;
}

bool
Server::matchIgnorelist(const sanecpp::device_info& info) const
{
  std::ifstream file(mIgnorelist);
  std::string line;
  while (std::getline(file, line)) {
    if (line.find('#') == 0)
      continue;
    if (line.find("//") == 0)
      continue;
    if (line.find(' ') == 0)
      continue;
    if (std::regex_match(info.name, std::regex(line))) {
      std::clog << mIgnorelist << ": regex '" << line << "'"
                << " matches device name '" << info.name << "'" << std::endl;
      return true;
    }
  }
  return false;
}

std::shared_ptr<MdnsPublisher::Service>
Server::buildMdnsService(const Scanner* pScanner)
{
  auto pService = std::make_shared<MdnsPublisher::Service>(&mPublisher);
  std::string type = "_uscan._tcp";
  if (mAnnouncesecure)
    type = "_uscans._tcp";
  pService->setType(type);
  pService->setName(pScanner->publishedName());
  pService->setInterfaceIndex(interfaceIndex());
  pService->setTxt("txtvers", "1");
  pService->setTxt("vers", "2.0");
  std::string s;
  for (const auto& f : pScanner->documentFormats())
    s += "," + f;
  if (!s.empty())
    pService->setTxt("pdl", s.substr(1));
  pService->setTxt("ty", pScanner->makeAndModel());
  if (pScanner->note().empty())
    pService->setTxt("note", mPublisher.hostname());
  else
    pService->setTxt("note", pScanner->note());
  pService->setTxt("uuid", pScanner->uuid());
  s = pScanner->uri();
  if (!s.empty() && s.front() == '/')
    s = s.substr(1);
  pService->setTxt("rs", s);
  s.clear();
  for (const auto& cs : pScanner->txtColorSpaces())
    s += "," + cs;
  if (!s.empty())
    pService->setTxt("cs", s.substr(1));
  s.clear();
  if (pScanner->hasPlaten())
    s += ",platen";
  if (pScanner->hasAdf())
    s += ",adf";
  if (!s.empty())
    pService->setTxt("is", s.substr(1));
  pService->setTxt("duplex", pScanner->hasDuplexAdf() ? "T" : "F");

  if (!pScanner->adminUrl().empty())
    pService->setTxt("adminurl", pScanner->adminUrl());
  if (!pScanner->iconUrl().empty())
    pService->setTxt("representation", pScanner->iconUrl());

  return pService;
}

void
Server::onRequest(const Request& request, Response& response)
{
  if (mWebinterface) {
      if (request.uri() == "/") {
        response.setStatus(HttpServer::HTTP_OK);
        response.setHeader(HttpServer::HTTP_HEADER_CONTENT_TYPE, "text/html");
        MainPage(mScanners, mResetoption, mDiscloseversion)
          .setTitle("AirSane Server on " + mPublisher.hostname())
          .render(request, response);
      } else if (request.uri() == "/reset" && mResetoption) {
        response.setStatus(HttpServer::HTTP_OK);
        response.setHeader(HttpServer::HTTP_HEADER_CONTENT_TYPE, "text/html");
        std::ostringstream oss;
        oss << ::ceil(mStartupTimeSeconds) + 1 << "; url=/";
        response.setHeader(HttpServer::HTTP_HEADER_REFRESH, oss.str());
        struct : WebPage
        {
          void onRender() override
          {
            out() << heading(1).addText(title()) << std::endl;
            out() << paragraph().addText(
                       "You will be redirected to the main page in a few seconds.")
                  << std::endl;
          }
        } resetpage;
        resetpage
          .setTitle("Resetting AirSane Server on " + mPublisher.hostname() + " ...")
          .render(request, response);
        this->terminate(SIGHUP);
      }
  }
  for (auto entry : mScanners) {// copy of entry is intended to protect scanner object
    if (request.uri().find(entry.pScanner->uri()) == 0) {
      std::string remainder = request.uri().substr(entry.pScanner->uri().length());
      handleScannerRequest(entry, remainder, request, response);
      return;
    }
  }
  HttpServer::onRequest(request, response);
}

void
Server::handleScannerRequest(ScannerList::value_type entry, const std::string& partialUri, const HttpServer::Request& request, HttpServer::Response& response)
{
  if ((partialUri.empty() || partialUri == "/") && mWebinterface) {
    response.setStatus(HttpServer::HTTP_OK);
    response.setHeader(HttpServer::HTTP_HEADER_CONTENT_TYPE, "text/html");
    ScannerPage(*entry.pScanner.get())
      .setTitle(entry.pScanner->publishedName() + " on " + mPublisher.hostname())
      .render(request, response);
    return;
  }
  if (partialUri == "/ScannerIcon" && request.method() == HttpServer::HTTP_GET) {
    std::ifstream file(entry.pScanner->iconFile());
    if (file.is_open()) {
      response.setHeader(HttpServer::HTTP_HEADER_CONTENT_TYPE,
                         HttpServer::MIME_TYPE_PNG);
      response.send() << file.rdbuf() << std::flush;
    } else {
      std::clog << "could not open " << entry.pScanner->iconFile()
                << " for reading" << std::endl;
      response.setStatus(HttpServer::HTTP_NOT_FOUND);
      response.send();
    }
    return;
  }
  if (partialUri == "/ScannerCapabilities" && request.method() == HttpServer::HTTP_GET) {
    response.setStatus(HttpServer::HTTP_OK);
    response.setHeader(HttpServer::HTTP_HEADER_CONTENT_TYPE, "text/xml");
    entry.pScanner->writeScannerCapabilitiesXml(response.send());
    return;
  }
  if (partialUri == "/ScannerStatus" && request.method() == HttpServer::HTTP_GET) {
    response.setStatus(HttpServer::HTTP_OK);
    response.setHeader(HttpServer::HTTP_HEADER_CONTENT_TYPE, "text/xml");
    entry.pScanner->writeScannerStatusXml(response.send());
    return;
  }
  static const std::string ScanJobsDir = "/ScanJobs";
  if (partialUri == ScanJobsDir && request.method() == HttpServer::HTTP_POST) {
    bool autoselectFormat = clientIsAirscan(request);
    std::shared_ptr<ScanJob> job = entry.pScanner->createJobFromScanSettingsXml(
        request.content(), autoselectFormat);
    if (job) {
      response.setStatus(HttpServer::HTTP_CREATED);
      response.setHeader(HttpServer::HTTP_HEADER_LOCATION, job->uri());
      response.send();
      return;
    }
  }
  if (partialUri.rfind(ScanJobsDir, 0) != 0) {
    HttpServer::onRequest(request, response);
    return;
  }
  std::string res = partialUri.substr(ScanJobsDir.length());
  if (res.empty() || res.front() != '/') {
    HttpServer::onRequest(request, response);
    return;
  }
  res = res.substr(1);
  size_t pos = res.find('/');
  if (pos > res.length()) {
    if (request.method() == HttpServer::HTTP_DELETE && entry.pScanner->cancelJob(res)) {
      response.setStatus(HttpServer::HTTP_OK);
      response.send();
      return;
    } else {
      HttpServer::onRequest(request, response);
      return;
    }
  }
  if (res.substr(pos) == "/NextDocument" && request.method() == HttpServer::HTTP_GET) {
    auto job = entry.pScanner->getJob(res.substr(0, pos));
    if (job) {
      if (job->isFinished()) {
        response.setStatus(HttpServer::HTTP_NOT_FOUND);
        response.send();
      } else {
        if (job->beginTransfer()) {
          response.setStatus(HttpServer::HTTP_OK);
          response.setHeader(HttpServer::HTTP_HEADER_CONTENT_TYPE, job->documentFormat());
          response.setHeader(HttpServer::HTTP_HEADER_TRANSFER_ENCODING, "chunked");
          job->finishTransfer(response.send());
        } else if (job->adfStatus() != SANE_STATUS_GOOD) {
          entry.pScanner->setTemporaryAdfStatus(job->adfStatus());
          response.setStatus(HttpServer::HTTP_CONFLICT);
          response.send();
        } else {
          response.setStatus(HttpServer::HTTP_SERVICE_UNAVAILABLE);
          response.send();
        }
      }
      return;
    }
  }
  HttpServer::onRequest(request, response);
}
