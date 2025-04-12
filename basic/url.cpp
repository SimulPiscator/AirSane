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

#include "url.h"

#define URL_TEST 0

#if URL_TEST
#include <sstream>

static const char* sTestCases[] =
{
  "",
  "http://user:password@host.org:1234/some/path/",
  "http://host.org:1234/some/path/",
  "http://user:password@host.org/some/path/",
  "http://user@host.org/some/path/",
  "http://host.org:1234",
  "http://host.org",
};
static struct UrlTest
{
  UrlTest()
  {
    for (auto pCase : sTestCases) {
      Url url(pCase);
      std::ostringstream oss;
      oss << url;
      if (oss.str() != pCase)
        std::cerr << "Url test failed: \n\t" << pCase << "\n\t" << oss.str() << std::endl;
    }
  }
} sUrlTest;
#endif

Url::Url(const std::string &url)
{
  size_t pos = url.find("://");
  if (pos != std::string::npos) {
    mProtocol = url.substr(0, pos);
    pos += 3;
  }
  else {
    pos = 0;
  }
  size_t pos2 = url.find("@", pos);
  if (pos2 != std::string::npos) {
    std::string userandpw = url.substr(pos, pos2 - pos);
    size_t pos3 = userandpw.find(":");
    if (pos3 != std::string::npos) {
      mUser = userandpw.substr(0, pos3);
      mPassword = userandpw.substr(pos3 + 1);
    }
    else {
      mUser = userandpw;
    }
    pos = pos2 + 1;
  }
  pos2 = url.find("/", pos);
  std::string hostandport = url.substr(pos, pos2 - pos);
  size_t pos3 = hostandport.find(":");
  if (pos3 != std::string::npos) {
    mHost = hostandport.substr(0, pos3);
    mPort = hostandport.substr(pos3 + 1);
  }
  else {
    mHost = hostandport;
  }
  if (pos2 != std::string::npos) {
    mPath = url.substr(pos2);
  }
}

std::ostream& Url::print(std::ostream& os) const
{
  if (!mProtocol.empty())
     os << mProtocol << "://";
  if (!mUser.empty()) {
    os << mUser;
    if (!mPassword.empty())
      os << ":" << mPassword;
    os << "@";
  }
  os << mHost;
  if (!mPort.empty())
      os << ":" << mPort;
  os << mPath;
  return os;
}
