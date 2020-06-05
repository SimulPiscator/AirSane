/*
AirSane Imaging Daemon
Copyright (C) 2018-2020 Simul Piscator

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

#ifndef SCANSERVER_H
#define SCANSERVER_H

#include "scanner.h"
#include "web/httpserver.h"
#include "zeroconf/mdnspublisher.h"
#include <vector>
#include <fstream>
#include <memory>
#include <magic.h>

typedef std::vector<std::pair<
    std::shared_ptr<Scanner>,
    std::shared_ptr<MdnsPublisher::Service>
>> ScannerList;

class ScanServer : public HttpServer
{
public:
    ScanServer(int argc, char** argv);
    ~ScanServer();
    bool run();

protected:
    void onRequest(const Request&, Response&) override;

private:
    void handleScannerRequest(ScannerList::value_type&, const std::string& uriRemainder, const HttpServer::Request&, HttpServer::Response&);

    MdnsPublisher mPublisher;
    ScannerList mScanners;
    std::filebuf mLogfile;
    bool mAnnounce, mLocalonly, mHotplug, mRandomUuids;
    std::string mOptionsfile;
    bool mDoRun;
    magic_t mMagicCookie;
};

#endif // SCANSERVER_H
