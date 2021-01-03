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

#include "mainserver.h"

#include <sstream>
#include <regex>
#include <csignal>
#include <ctime>

#include "scanner.h"
#include "scanjob.h"
#include "mainpage.h"
#include "optionsfile.h"
#include "basic/hostname.h"
#include "zeroconf/hotplugnotifier.h"

namespace {

    struct Notifier : HotplugNotifier
    {
        MainServer& server;
        explicit Notifier(MainServer& s) : server(s) {}
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


MainServer::MainServer(int argc, char** argv)
    : mAnnounce(true), mLocalonly(true), mHotplug(true),
      mRandomUuids(false), mDoRun(true), mStartupTimeSeconds(0)
{
    std::string port, interface, accesslog, hotplug, announce,
        localonly, optionsfile, ignorelist, randomuuids, debug;
    struct { const std::string name, def, info; std::string& value; } options[] = {
    { "base-port", "8090", "base listening port", port },
    { "interface", "", "listen on named interface only", interface },
    { "access-log", "", "HTTP access log, - for stdout", accesslog },
    { "hotplug", "true", "repeat scanner search on hotplug event", hotplug },
    { "mdns-announce", "true", "announce scanners via mDNS", announce },
    { "local-scanners-only", "true", "ignore SANE network scanners", localonly },
    { "options-file", "/etc/airsane/options.conf", "location of device options file", optionsfile },
    { "ignore-list", "/etc/airsane/ignore.conf", "location of device ignore list", ignorelist },
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
    mIgnorelist = ignorelist;
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

MainServer::~MainServer()
{
}

bool MainServer::run()
{
    if(!mDoRun)
        return false;
    
    struct timespec t = {0};
    ::clock_gettime(CLOCK_MONOTONIC, &t);
    float t0 = t.tv_sec + 1e-9 * t.tv_nsec;

    std::shared_ptr<Notifier> pNotifier;
    if(mHotplug)
        pNotifier = std::make_shared<Notifier>(*this);

    bool ok = false, done = false;
    do {
        OptionsFile optionsfile(mOptionsfile);
        std::clog << "enumerating " << (mLocalonly ? "local " : " ") << "devices..." << std::endl;
        auto scanners = sanecpp::enumerate_devices(mLocalonly);
        uint16_t port = MainServer::port();
        for(const auto& s : scanners) {
            std::clog << "found: " << s.name << " (" << s.vendor << " " << s.model << ")" << std::endl;
            if(matchIgnorelist(s)) {
                std::clog << "ignoring " << s.name << std::endl;
                continue;
            }
            auto pScanner = std::make_shared<Scanner>(s, mRandomUuids);
            if(pScanner->error())
                std::clog << "error: " << pScanner->error() << std::endl;
            else {
                std::clog << "stable unique name: " << pScanner->stableUniqueName() << std::endl;
                std::clog << "uuid: " << pScanner->uuid() << std::endl;

                auto options = optionsfile.scannerOptions(pScanner.get());
                pScanner->setDeviceOptions(options);

                ++port;
                std::ostringstream url;
                url << "http://"
                    << hostnameFqdn()
                    << ":" << port
                    << "/";
                pScanner->setAdminUrl(url.str());
                if(!pScanner->iconFile().empty()) {
                    url << "ScannerIcon";
                    pScanner->setIconUrl(url.str());
                }
            }

            std::shared_ptr<MdnsPublisher::Service> pService;
            if(mAnnounce && !pScanner->error())
            {
                pService = buildMdnsService(pScanner.get());
                pService->setPort(port);
                if(pService->announce()) {
                    std::clog << "published as '" << pService->name() << "'" << std::endl;
                    pScanner->setPublishedName(pService->name());
                }
                else
                    pService.reset();
            }
            
            std::shared_ptr<ScannerServer> pServer = std::make_shared<ScannerServer>(pScanner, port);
            mScanners.push_back(ScannerEntry({pScanner, pService, pServer}));
        }
        ::clock_gettime(CLOCK_MONOTONIC, &t);
        float t1 = t.tv_sec + 1e-9 * t.tv_nsec;
        mStartupTimeSeconds = t1 - t0;

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

bool MainServer::matchIgnorelist(const sanecpp::device_info& info) const
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
                      << " matches device name '" << info.name << "'" 
                      << std::endl;
            return true;
        }
    }
    return false;
}

std::shared_ptr<MdnsPublisher::Service> MainServer::buildMdnsService(const Scanner* pScanner)
{
    auto pService = std::make_shared<MdnsPublisher::Service>(&mPublisher);
    pService->setType("_uscan._tcp.");
    pService->setName(pScanner->publishedName());
    pService->setInterfaceIndex(interfaceIndex());
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
    pService->setTxt("rs", "eSCL");
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

void MainServer::onRequest(const Request& request, Response& response)
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
        std::ostringstream oss;
        oss << ::ceil(mStartupTimeSeconds) + 1 << "; url=/";
        response.setHeader(HttpServer::HTTP_HEADER_REFRESH, oss.str());
        struct : WebPage
        {
          void onRender() override {
            out() << heading(1).addText(title()) << std::endl;
            out() << paragraph().addText("You will be redirected to the main page in a few seconds.") << std::endl;
          }
        } resetpage;
        resetpage.setTitle("Resetting AirSane Server on " + hostname() + " ...")
                 .render(request, response);
        this->terminate(SIGHUP);
    }
    HttpServer::onRequest(request, response);
}
