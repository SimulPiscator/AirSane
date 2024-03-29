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

#ifndef ACCESS_FILE_H
#define ACCESS_FILE_H

#include "web/httpserver.h"
#include <string>
#include <vector>

class AccessFile
{
public:
  AccessFile() = default;
  AccessFile(const AccessFile&) = default;
  AccessFile& operator=(const AccessFile&) = default;
  ~AccessFile() = default;

  explicit AccessFile(const std::string& path);

  const std::string& errors() const;
  bool isAllowed(const HttpServer::Sockaddr&) const;

 private:
  class Entry
  {
   public:
    enum { NoMatch, Allow, Deny };
    int match(const HttpServer::Sockaddr&) const;
    std::istream& parse(std::istream&);
   private:
    int mKind;
    std::string mRule;
    struct Network
    {
      HttpServer::Sockaddr address, mask;
    };
    std::vector<Network> mNetworks;
  };
  std::vector<Entry> mEntries;
  std::string mErrors;
};

#endif // ACCESS_FILE_H
