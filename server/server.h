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

#ifndef SERVER_H
#define SERVER_H

#include "scanner.h"
#include "web/httpserver.h"
#include "zeroconf/mdnspublisher.h"
#include <fstream>
#include <memory>
#include <tuple>
#include <vector>

struct ScannerEntry
{
  std::shared_ptr<Scanner> pScanner;
  std::shared_ptr<MdnsPublisher::Service> pService;
};
typedef std::vector<ScannerEntry> ScannerList;

class Server : public HttpServer
{
public:
  Server(int argc, char** argv);
  ~Server();
  bool run();

protected:
  void onRequest(const Request&, Response&) override;

private:
  void chooseUniquePublishedName(Scanner*) const;
  bool publishedNameExists(const std::string&) const;
  bool matchIgnorelist(const sanecpp::device_info&) const;
  std::shared_ptr<MdnsPublisher::Service> buildMdnsService(const Scanner*);
  // must pass ScannerList element by value to achieve protection during
  // request
  void handleScannerRequest(ScannerList::value_type,
                            const std::string& uriRemainder,
                            const HttpServer::Request&,
                            HttpServer::Response&);

  MdnsPublisher mPublisher;
  ScannerList mScanners;
  std::filebuf mLogfile;
  bool mAnnounce, mWebinterface, mResetoption, mDiscloseversion,
    mLocalonly, mHotplug, mRandompaths, mCompatiblepath, mAnnouncesecure;
  std::string mOptionsfile, mIgnorelist;
  float mStartupTimeSeconds;
  bool mDoRun;
};

#endif // SERVER_H
