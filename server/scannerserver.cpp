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

#include "scannerserver.h"
#include "scannerpage.h"
#include "scanjob.h"

#include <fstream>
#include <csignal>

ScannerServer::ScannerServer(std::shared_ptr<Scanner> pScanner, uint16_t port)
: mpScanner(pScanner), mpThread(nullptr)
{
    HttpServer::setPort(port);
    mpThread = new std::thread([this]{this->run();});
}

ScannerServer::~ScannerServer()
{
    HttpServer::terminate(SIGTERM);
    mpThread->join();
    delete mpThread;
}

static bool clientIsAirscan(const HttpServer::Request& req)
{
    return req.header(HttpServer::HTTP_HEADER_USER_AGENT).find("AirScanScanner") != std::string::npos;
}

void ScannerServer::onRequest(const HttpServer::Request &request, HttpServer::Response &response)
{
    if (request.uri().empty() || request.uri() == "/") {
        response.setStatus(HttpServer::HTTP_OK);
        response.setHeader(HttpServer::HTTP_HEADER_CONTENT_TYPE, "text/html");
        ScannerPage(*mpScanner.get()).setTitle(mpScanner->publishedName() + " on " + hostname()).render(request, response);
        return;
    }
    if (request.uri() == "/ScannerIcon" && request.method() == HttpServer::HTTP_GET) {
        std::ifstream file(mpScanner->iconFile());
        if(file.is_open()) {
            response.setHeader(HttpServer::HTTP_HEADER_CONTENT_TYPE, HttpServer::MIME_TYPE_PNG);
            response.send() << file.rdbuf() << std::flush;
        }
        else {
            std::clog << "could not open " << mpScanner->iconFile()
                      << " for reading" << std::endl;
            response.setStatus(HttpServer::HTTP_NOT_FOUND);
            response.send();
        }
        return;
    }
    static const std::string escl = "/eSCL";
    if (request.uri().find(escl) == 0) {
        std::string uriRemainder = request.uri().substr(escl.length());
        if(uriRemainder == "/ScannerCapabilities" && request.method() == HttpServer::HTTP_GET) {
            response.setStatus(HttpServer::HTTP_OK);
            response.setHeader(HttpServer::HTTP_HEADER_CONTENT_TYPE, "text/xml");
            mpScanner->writeScannerCapabilitiesXml(response.send());
            return;
        }
        if(uriRemainder == "/ScannerStatus" && request.method() == HttpServer::HTTP_GET) {
            response.setStatus(HttpServer::HTTP_OK);
            response.setHeader(HttpServer::HTTP_HEADER_CONTENT_TYPE, "text/xml");
            mpScanner->writeScannerStatusXml(response.send());
            return;
        }
        static const std::string ScanJobsDir = "/ScanJobs";
        if(uriRemainder == ScanJobsDir && request.method() == HttpServer::HTTP_POST) {
            bool autoselectFormat = clientIsAirscan(request);
            std::shared_ptr<ScanJob> job = mpScanner->createJobFromScanSettingsXml(request.content(), autoselectFormat);
            if(job) {
                response.setStatus(HttpServer::HTTP_CREATED);
                response.setHeader(HttpServer::HTTP_HEADER_LOCATION, job->uri());
                response.send();
                return;
            }
        }
        if(uriRemainder.rfind(ScanJobsDir, 0) != 0) {
            HttpServer::onRequest(request, response);
            return;
        }
        std::string res = uriRemainder.substr(ScanJobsDir.length());
        if(res.empty() || res.front() != '/') {
            HttpServer::onRequest(request, response);
            return;
        }
        res = res.substr(1);
        size_t pos = res.find('/');
        if(pos > res.length() && request.method() == HttpServer::HTTP_DELETE && mpScanner->cancelJob(res)) {
            response.setStatus(HttpServer::HTTP_OK);
            response.send();
            return;
        }
        if(res.substr(pos) == "/NextDocument" && request.method() == HttpServer::HTTP_GET) {
            auto job = mpScanner->getJob(res.substr(0, pos));
            if(job) {
                if(job->isFinished()) {
                    response.setStatus(HttpServer::HTTP_NOT_FOUND);
                    response.send();
                } else {
                    if(job->beginTransfer()) {
                        response.setStatus(HttpServer::HTTP_OK);
                        response.setHeader(HttpServer::HTTP_HEADER_CONTENT_TYPE, job->documentFormat());
                        response.setHeader(HttpServer::HTTP_HEADER_TRANSFER_ENCODING, "chunked");
                        job->finishTransfer(response.send());
                    } else if(job->adfStatus() != SANE_STATUS_GOOD) {
                        mpScanner->setTemporaryAdfStatus(job->adfStatus());
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
    HttpServer::onRequest(request, response);
}


