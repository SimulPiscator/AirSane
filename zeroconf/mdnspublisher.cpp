/*
AirSane Imaging Daemon
Copyright (C) 2018-2022 Simul Piscator

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

#include "mdnspublisher.h"

#include <algorithm>

MdnsPublisher::Service&
MdnsPublisher::Service::setName(const std::string& s)
{
  std::lock_guard<std::mutex> lock(mNameMutex);
  mName = s;
  return *this;
}

std::string
MdnsPublisher::Service::name() const
{
  std::lock_guard<std::mutex> lock(mNameMutex);
  return mName;
}

MdnsPublisher::Service&
MdnsPublisher::Service::setTxt(const std::string& key, const std::string& value)
{
  if (!key.empty()) {
    auto i = std::find_if(
      mTxtRecord.begin(),
      mTxtRecord.end(),
      [key](const TxtRecord::value_type& v) { return v.first == key; });
    if (i == mTxtRecord.end())
      mTxtRecord.push_back(std::make_pair(key, value));
    else
      i->second = value;
  }
  return *this;
}
