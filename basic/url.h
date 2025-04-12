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

#ifndef URL_H
#define URL_H

#include <string>
#include <iostream>

class Url
{
public:
  explicit Url(const std::string&);

  const std::string& protocol() const { return mProtocol; }
  const std::string& host() const { return mHost; }
  const std::string& port() const { return mPort; }
  const std::string& user() const { return mUser; }
  const std::string& password() const { return mPassword; }
  const std::string& path() const { return mPath; }

  std::ostream& print(std::ostream&) const;

private:
  std::string mProtocol, mHost, mPort, mUser, mPassword, mPath;
};

inline std::ostream& operator<<(std::ostream& os, const Url& url) { return url.print(os); }

#endif // URL_H
