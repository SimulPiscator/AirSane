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

#include "accessfile.h"
#include <fstream>
#include <sstream>
#include <cstring>
#include <cassert>

namespace {
  bool SetMaskBits(HttpServer::Sockaddr& mask, int bits)
  {
    int width = 0;
    char* data = nullptr;
    if (mask.sa.sa_family == AF_INET) {
      width = 32;
      data = reinterpret_cast<char*>(&mask.in.sin_addr);
    }
    else if (mask.sa.sa_family == AF_INET6) {
      width = 128;
      data = reinterpret_cast<char*>(&mask.in6.sin6_addr);
    }

    if (bits > width)
      return false;
    if (!data)
      return false;

    ::memset(data, width / 8, 0);
    for (int i = 0; i < bits; ++i) {
      int byte = i / 8, bit = i % 8;
      data[byte] |= (1 << bit);
    }
    return true;
  }

  bool MatchAddresses(const HttpServer::Sockaddr& inAddr1, const HttpServer::Sockaddr& inAddr2, const HttpServer::Sockaddr& inMask)
  {
    if (inAddr1.sa.sa_family != inAddr2.sa.sa_family)
      return false;
    assert(inAddr2.sa.sa_family == inMask.sa.sa_family);

    int width = 0;
    const char* data1 = nullptr, *data2 = nullptr, *maskdata = nullptr;
    if (inMask.sa.sa_family == AF_INET) {
      width = 32;
      data1 = reinterpret_cast<const char*>(&inAddr1.in.sin_addr);
      data2 = reinterpret_cast<const char*>(&inAddr2.in.sin_addr);
      maskdata = reinterpret_cast<const char*>(&inMask.in.sin_addr);
    }
    else if (inMask.sa.sa_family == AF_INET6) {
      width = 128;
      data1 = reinterpret_cast<const char*>(&inAddr1.in6.sin6_addr);
      data2 = reinterpret_cast<const char*>(&inAddr2.in6.sin6_addr);
      maskdata = reinterpret_cast<const char*>(&inMask.in6.sin6_addr);
    }
    for (int i = 0; i < width / 8; ++i) {
      char c1 = data1[i] & maskdata[i],
          c2 = data2[i] & maskdata[i];
      if (c1 != c2)
        return false;
    }
    return true;
  }
}

AccessFile::AccessFile(const std::string& path)
{
  if (path.empty())
    return;
  std::ifstream file(path);
  if (!file.is_open())
    return;
  std::string line;
  while (std::getline(file, line)) {
    while (!line.empty() && std::iswspace(line.front()))
      line = line.substr(1);
    while (!line.empty() && std::iswspace(line.back()))
      line.pop_back();
    if (line.empty())
      continue;
    if (line.front() == '#')
      continue;
    std::istringstream iss(line);
    Entry entry;
    if (!entry.parse(iss))
      mErrors += "Illegal entry: " + line + "\n";
    else
      mEntries.push_back(entry);
  }
}

const std::string& AccessFile::errors() const
{
  return mErrors;
}

bool AccessFile::isAllowed(const HttpServer::Sockaddr& addr) const
{
  if (mEntries.empty())
    return true;
  for (const auto& entry : mEntries) {
    int result = entry.match(addr);
    if (result == Entry::Allow)
      return true;
    if (result == Entry::Deny)
      return false;
  }
  return false;
}

std::istream& AccessFile::Entry::parse(std::istream& is)
{
  std::string kind, address;
  is >> kind;
  if (!::strcasecmp(kind.c_str(), "allow"))
    mKind = Allow;
  else if (!::strcasecmp(kind.c_str(), "deny"))
    mKind = Deny;
  else
    is.setstate(std::ios::failbit);
  std::getline(is >> std::ws, address);
  int bits = -1;
  size_t pos = address.find_last_of("/");
  if (pos != std::string::npos) {
    bits = ::atoi(address.substr(pos + 1).c_str());
    address = address.substr(0, pos);
  }
  if (::inet_pton(AF_INET, address.c_str(), &mAddress.in.sin_addr)) {
    if (bits == -1)
      bits = 32;
    mAddress.sa.sa_family = AF_INET;
    mMask = mAddress;
    SetMaskBits(mMask, bits);
  }
  else if (::inet_pton(AF_INET6, address.c_str(), &mAddress.in6.sin6_addr)) {
    if (bits == -1)
      bits = 128;
    mAddress.sa.sa_family = AF_INET6;
    mMask = mAddress;
    SetMaskBits(mMask, bits);
  }
  else {
    is.setstate(std::ios::failbit);
  }
  return is;
}

int AccessFile::Entry::match(const HttpServer::Sockaddr &addr) const
{
  if (!MatchAddresses(addr, mAddress, mMask))
    return NoMatch;
  return mKind;
}
