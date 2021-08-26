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

#include "uuid.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <functional>
#include <iomanip>
#include <unistd.h>

namespace {
std::string
getMachineId()
{
  std::string id;
  std::getline(std::ifstream("/etc/machine-id"), id);
  if (id.empty()) {
    char buf[1024] = { 0 };
    if (!::gethostname(buf, sizeof(buf) - 1))
      id = buf;
  }
  return id;
}
const std::string sMachineId = getMachineId();

void
printByteGroup(const char* data, int begin, int end, std::ostream& os)
{
  for (auto p = data + begin; p < data + end; ++p)
    os << (*p >> 4 & 0xf) << (*p & 0xf);
}
} // namespace

Uuid::Uuid()
{
  ::memset(mData, 0, sizeof(mData));
}

std::ostream&
Uuid::print(std::ostream& os) const
{
  os << std::hex << std::noshowbase;
  printByteGroup(mData, 0, 4, os);
  os << "-";
  printByteGroup(mData, 4, 6, os);
  os << "-";
  printByteGroup(mData, 6, 8, os);
  os << "-";
  printByteGroup(mData, 8, 10, os);
  os << "-";
  printByteGroup(mData, 10, sizeof(mData), os);
  return os;
}

std::string
Uuid::toString() const
{
  std::ostringstream oss;
  print(oss);
  return oss.str();
}

void
Uuid::initFromString(const std::string& inStringData)
{
  // Make sure UUID bytes are not
  // too obviously related to original string content.
  auto hashfn = std::hash<std::string>();
  std::string s = sMachineId + inStringData;
  union
  {
    size_t h;
    char c[sizeof(h)];
  } hash;
  size_t pos = 0;
  while (pos < s.length()) {
    hash.h = hashfn(s);
    for (size_t i = 0; i < sizeof(hash) && pos + i < s.length(); ++i)
      s[pos + i] ^= hash.c[i];
    pos += sizeof(hash);
  }
  // Fill any trailing zero bytes with data.
  while (s.length() < sizeof(mData)) {
    hash.h = hashfn(s);
    s += std::string(hash.c, sizeof(hash));
  }
  ::memset(mData, 0, sizeof(mData));
  for (size_t i = 0; i < s.length(); ++i)
    mData[i % sizeof(mData)] ^= s[i];
  // Mark the UUID as version 5 (which is closest to our case).
  mData[6] &= 0x0f;
  mData[6] |= 0x50;
  mData[8] &= 0x3f;
  mData[8] |= 0x80;
}

Uuid Uuid::Random()
{
  Uuid uuid;
  std::ifstream random("/dev/random");
  if (!random.is_open())
    random.open("/dev/urandom");
  if (!random.is_open()) {
    std::cerr << "could not open /dev/random or /dev/urandom for reading" << std::endl;
    ::exit(-1);
  }
  random.read(uuid.mData, sizeof(uuid.mData));
  // Mark the UUID as version 4, variant 1.
  uuid.mData[6] &= 0x0f;
  uuid.mData[6] |= 0x40;
  uuid.mData[8] &= 0x3f;
  uuid.mData[8] |= 0x80;
  return uuid;
}

