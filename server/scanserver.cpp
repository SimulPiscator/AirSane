/*
AirSane Imaging Daemon
Copyright (C) 2018-2021 Simul Piscator

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

#include "scanserver.h"

#include <fstream>
#include <sstream>
#include <csignal>
#include <cstring>
#include <algorithm>
#include <regex>

#include "scanner.h"
#include "scanjob.h"
#include "mainpage.h"
#include "scannerpage.h"
#include "optionsfile.h"
#include "zeroconf/hotplugnotifier.h"

namespace {

    const char* ignoreList[] =
    {
        // Avoid device detection loops from escl backends.
        "escl:.*", "airscan:.*",
    };
    bool applyIgnoreList(const sanecpp::device_info& info)
    {
        for(const auto pattern : ignoreList)
            if(std::regex_match(info.name, std::regex(pattern)))
                return true;
        return false;
    }

    struct Notifier : HotplugNotifier
    {
        ScanServer& server;
        explicit Notifier(ScanServer& s) : server(s) {}
        void onHotplugEvent(Event ev) override
        {
            switch(ev) {
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
} // namespace


ScanServer::ScanServer(int argc, char** argv)
    : mAnnounce(true), mLocalonly(true), mHotplug(true),
      mRandomUuids(false), mDoRun(true)
{
    std::string port, interface, accesslog, hotplug, announce,
        localonly, optionsfile, randomuuids, debug;
    struct { const std::string name, def, info; std::string& value; } options[] = {
    { "listen-port", "8090", "listening port", port },
    { "interface", "", "listen on named interface only", interface },
    { "access-log", "", "HTTP access log, - for stdout", accesslog },
    { "hotplug", "true", "reload scanner list on hotplug event", hotplug },
    { "mdns-announce", "true", "announce scanners via mDNS (avahi)", announce },
    { "local-scanners-only", "true", "ignore SANE network scanners", localonly },
    { "options-file", "/etc/airsane/options.conf", "location of device options file", optionsfile },
    { "random-uuids", "false", "generate random UUIDs on startup", randomuuids },
    { "debug", "false", "log debug information to stderr", debug },
    };
    for(auto& opt : options)
        opt.value = opt.def;
    bool help = false;
    for(int i = 1; i < argc; ++i) {
        std::string option = argv[i];
        bool found = false;
        for(auto& opt : options) {
            if(option.rfind("--" + opt.name + "=", 0) == 0) {
                found = true;
                opt.value = option.substr(option.find('=') + 1);
                break;
            } else if(option == "--" + opt.name) {
                found = true;
                help = true;
                std::cerr << "missing argument for option " << option << std::endl;
                break;
            }
        }
        if(!found) {
            help = true;
            if(option != "--help")
                std::cerr << "unknown option: " << option << std::endl;
        }
    }

    if(debug != "true")
        std::clog.rdbuf(nullptr);
    sanecpp::log.rdbuf(std::clog.rdbuf());

    mHotplug = (hotplug == "true");
    mAnnounce = (announce == "true");
    mLocalonly = (localonly == "true");
    mOptionsfile = optionsfile;
    mRandomUuids = (randomuuids == "true");

    uint16_t port_ = 0;
    if(!(std::istringstream(port) >> port_)) {
        std::cerr << "invalid port number: " << port << std::endl;
        mDoRun = false;
    }
    if(help) {
        std::cout << "options, and their defaults, are:\n";
        for(auto& opt : options)
            std::cout << " --" << opt.name << "=" << opt.def << "\t" << opt.info << "\n";
        std::cout << " --help\t" << "show this help" << std::endl;
        mDoRun = false;
    }
    if(mDoRun) {
        if(!interface.empty())
            setInterfaceName(interface);
        setPort(port_);
        if(accesslog.empty())
            std::cout.rdbuf(nullptr);
        else if(accesslog != "-")
            std::cout.rdbuf(mLogfile.open(accesslog, std::ios::app));
    }
}

ScanServer::~ScanServer()
{
}

bool ScanServer::run()
{
    if(!mDoRun)
        return false;

    std::shared_ptr<Notifier> pNotifier;
    if(mHotplug)
        pNotifier = std::make_shared<Notifier>(*this);

    bool ok = false, done = false;
    do {
        OptionsFile optionsfile(mOptionsfile);
        std::clog << "enumerating " << (mLocalonly ? "local " : " ") << "devices..." << std::endl;
        auto scanners = sanecpp::enumerate_devices(mLocalonly);
        for(const auto& s : scanners) {
            std::clog << "found: " << s.name << " (" << s.vendor << " " << s.model << ")" << std::endl;
            if(applyIgnoreList(s))
                std::clog << "ignoring " << s.name << std::endl;
            auto pScanner = std::make_shared<Scanner>(s, mRandomUuids);
            if(pScanner->error())
                std::clog << "error: " << pScanner->error() << std::endl;
            else {
                std::clog << "stable unique name: " << pScanner->stableUniqueName() << std::endl;
                std::clog << "uuid: " << pScanner->uuid() << std::endl;

                auto options = optionsfile.scannerOptions(pScanner.get());
                pScanner->setDeviceOptions(options);

                std::ostringstream url;
                url << "http://"
                    << mPublisher.hostNameFqdn()
                    << ":" << port()
                    << pScanner->uri();
                pScanner->setAdminUrl(url.str());
                if(!pScanner->iconFile().empty()) {
                    url << "/ScannerIcon";
                    pScanner->setIconUrl(url.str());
                }
            }

            std::shared_ptr<MdnsPublisher::Service> pService;
            if(mAnnounce && !pScanner->error())
            {
                pService = buildMdnsService(pScanner.get());
                if(pService->announce())
                    std::clog << "published as '" << pService->name() << "'" << std::endl;
                else
                    pService.reset();
            }
            mScanners.push_back(std::make_pair(pScanner, pService));
        }
        ok = HttpServer::run();
        mScanners.clear();
        if(ok && terminationStatus() == SIGHUP) {
            std::clog << "received SIGHUP, reloading" << std::endl;
        } else if(ok && terminationStatus() == SIGTERM) {
            std::clog << "received SIGTERM, exiting" << std::endl;
            done = true;
        } else {
            ok = false;
            done = true;
        }
    } while(!done);
    if(ok) {
        std::clog << "server finished ok" << std::endl;
    } else {
        std::cerr << "server finished with error status "
                  << terminationStatus() << ", last error was "
                  << lastError() << ": " << ::strerror(lastError())
                  << std::endl;
    }
    return ok;
}

std::shared_ptr<MdnsPublisher::Service> ScanServer::buildMdnsService(const Scanner* pScanner)
{
    auto pService = std::make_shared<MdnsPublisher::Service>(&mPublisher);
    pService->setType("_uscan._tcp.");
    pService->setName(pScanner->makeAndModel());
    pService->setInterfaceIndex(interfaceIndex()).setPort(port());
    pService->setTxt("txtvers", "1");
    pService->setTxt("vers", "2.0");
    std::string s;
    for(const auto& f : pScanner->documentFormats())
        s += "," + f;
    if(!s.empty())
        pService->setTxt("pdl", s.substr(1));
    pService->setTxt("ty", pScanner->makeAndModel());
    pService->setTxt("note", hostname());
    pService->setTxt("uuid", pScanner->uuid());
    pService->setTxt("rs", pScanner->uri().substr(1));
    s.clear();
    for(const auto& cs : pScanner->txtColorSpaces())
        s += "," + cs;
    if(!s.empty())
        pService->setTxt("cs", s.substr(1));
    s.clear();
    if(pScanner->hasPlaten())
        s += ",platen";
    if(pScanner->hasAdf())
        s += ",adf";
    if(!s.empty())
        pService->setTxt("is", s.substr(1));
    pService->setTxt("duplex", pScanner->hasDuplexAdf() ? "T" : "F");

    if(!pScanner->adminUrl().empty())
        pService->setTxt("adminurl", pScanner->adminUrl());
    if(!pScanner->iconUrl().empty())
        pService->setTxt("representation", pScanner->iconUrl());

    return pService;
}

void ScanServer::onRequest(const Request& request, Response& response)
{
    if(request.uri() == "/") {
        response.setStatus(HttpServer::HTTP_OK);
        response.setHeader(HttpServer::HTTP_HEADER_CONTENT_TYPE, "text/html");
        MainPage(mScanners)
                .setTitle("AirSane Server on " + hostname())
                .render(request, response);
    }
    else if(request.uri() == "/reset") {
        response.setStatus(HttpServer::HTTP_OK);
        response.setHeader(HttpServer::HTTP_HEADER_CONTENT_TYPE, "text/html");
        response.setHeader(HttpServer::HTTP_HEADER_REFRESH, "2; url=/");
        struct : WebPage
        {
          void onRender() override {
            out() << heading(1).addText(title()) << std::endl;
            out() << paragraph().addText("You will be redirected to the main page in a few seconds.") << std::endl;
          }
        } resetpage;
        resetpage.setTitle("Resetting AirSane Server on " + hostname() + "...")
                 .render(request, response);
        this->terminate(SIGHUP);
    }
    for(auto& s : mScanners) {
        if(request.uri().find(s.first->uri()) == 0) {
            std::string res = request.uri().substr(s.first->uri().length());
            handleScannerRequest(s, res, request, response);
            return;
        }
        else if(request.uri().find("/" + s.first->uuid()) == 0) {
            std::string res = request.uri().substr(s.first->uuid().length() + 1);
            handleScannerRequest(s, res, request, response);
            return;
        }
    }
    HttpServer::onRequest(request, response);
}

static bool clientIsAirscan(const HttpServer::Request& req)
{
    return req.header(HttpServer::HTTP_HEADER_USER_AGENT).find("AirScanScanner") != std::string::npos;
}

void ScanServer::handleScannerRequest(ScannerList::value_type s, const std::string& uriRemainder, const HttpServer::Request &request, HttpServer::Response &response)
{
    response.setStatus(HttpServer::HTTP_OK);
    if(uriRemainder.empty() || uriRemainder == "/") {
        response.setHeader(HttpServer::HTTP_HEADER_CONTENT_TYPE, "text/html");
        auto name = s.second ? s.second->name() : s.first->makeAndModel();
        ScannerPage(*s.first).setTitle(name + " on " + hostname()).render(request, response);
        return;
    }
    if(uriRemainder == "/ScannerCapabilities" && request.method() == HttpServer::HTTP_GET) {
        response.setHeader(HttpServer::HTTP_HEADER_CONTENT_TYPE, "text/xml");
        s.first->writeScannerCapabilitiesXml(response.send());
        return;
    }
    if(uriRemainder == "/ScannerStatus" && request.method() == HttpServer::HTTP_GET) {
        response.setHeader(HttpServer::HTTP_HEADER_CONTENT_TYPE, "text/xml");
        s.first->writeScannerStatusXml(response.send());
        return;
    }
    if(uriRemainder == "/ScannerIcon" && request.method() == HttpServer::HTTP_GET) {
        std::ifstream file(s.first->iconFile());
        if(file.is_open()) {
            response.setHeader(HttpServer::HTTP_HEADER_CONTENT_TYPE, HttpServer::MIME_TYPE_PNG);
            response.send() << file.rdbuf() << std::flush;
        }
        else {
            std::clog << "could not open " << s.first->iconFile()
                      << " for reading" << std::endl;
            response.setStatus(HttpServer::HTTP_NOT_FOUND);
            response.send();
        }
        return;
    }
    const std::string ScanJobsDir = "/ScanJobs";
    if(uriRemainder == ScanJobsDir && request.method() == HttpServer::HTTP_POST) {
        bool autoselectFormat = clientIsAirscan(request);
        std::shared_ptr<ScanJob> job = s.first->createJobFromScanSettingsXml(request.content(), autoselectFormat);
        if(job) {
            response.setStatus(HttpServer::HTTP_CREATED);
            response.setHeader(HttpServer::HTTP_HEADER_LOCATION, job->uri());
            response.send();
            return;
        }
    }
    if(uriRemainder.rfind(ScanJobsDir, 0) != 0)
        return;
    std::string res = uriRemainder.substr(ScanJobsDir.length());
    if(res.empty() || res.front() != '/')
        return;
    res = res.substr(1);
    size_t pos = res.find('/');
    if(pos > res.length() && request.method() == HttpServer::HTTP_DELETE && s.first->cancelJob(res)) {
        response.send();
        return;
    }
    if(res.substr(pos) == "/NextDocument" && request.method() == HttpServer::HTTP_GET) {
        auto job = s.first->getJob(res.substr(0, pos));
        if(job) {
            if(job->isFinished()) {
                response.setStatus(HttpServer::HTTP_NOT_FOUND);
                response.send();
            } else {
                if(job->beginTransfer()) {
                    response.setHeader(HttpServer::HTTP_HEADER_CONTENT_TYPE, job->documentFormat());
                    response.setHeader(HttpServer::HTTP_HEADER_TRANSFER_ENCODING, "chunked");
                    job->finishTransfer(response.send());
                } else if(job->adfStatus() != SANE_STATUS_GOOD) {
                    s.first->setTemporaryAdfStatus(job->adfStatus());
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
}

