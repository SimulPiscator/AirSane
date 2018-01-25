/*
AirSane Imaging Daemon
Copyright (C) 2018 Simul Piscator

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

#include "dictionary.h"
#include <sstream>
#include <locale>
#include <limits>
#include <algorithm>

namespace {

std::string numtostr(double num)
{
    std::ostringstream oss;
    oss.imbue(std::locale("C"));
    oss << num;
    return oss.str();
}

double strtonum(const std::string& s)
{
    std::istringstream iss(s);
    iss.imbue(std::locale("C"));
    double num = std::numeric_limits<double>::quiet_NaN();
    iss >> num;
    return num;
}

const std::string emptystring;

Dictionary::Storage::iterator find(Dictionary::Storage& s, const std::string& key)
{
  return std::find_if(s.begin(), s.end(), [key](const Dictionary::Storage::value_type& v){ return v.first == key; });
}

Dictionary::Storage::const_iterator find(const Dictionary::Storage& s, const std::string& key)
{
  return std::find_if(s.begin(), s.end(), [key](const Dictionary::Storage::value_type& v){ return v.first == key; });
}

}


bool Dictionary::hasKey(const std::string &key) const
{
    auto i = find(mData, key);
    return i != mData.end();
}

void Dictionary::eraseKey(const std::string &key)
{
    auto i = find(mData, key);
    if(i != mData.end())
        mData.erase(i);
}

const std::string &Dictionary::applyDefaultValue(const std::string &key, const std::string &value)
{
    auto i = find(mData, key);
    if(i == mData.end())
        i = mData.insert(mData.end(), std::make_pair(key, value));
    return i->second;
}

const std::string &Dictionary::applyDefaultValue(const std::string &key, double value)
{
    return applyDefaultValue(key, numtostr(value));
}

double Dictionary::getNumber(const std::string & s) const
{
    return strtonum(getString(s));
}

const std::string &Dictionary::getString(const std::string &key) const
{
    auto i = find(mData, key);
    return i == mData.end() ? emptystring : i->second;
}

std::string &Dictionary::operator[](const std::string &key)
{
    auto i = find(mData, key);
    if(i == mData.end())
        i = mData.insert(mData.end(), std::make_pair(key, ""));
    return i->second;
}
