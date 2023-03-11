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

#ifndef DICTIONARY_H
#define DICTIONARY_H

#include <string>
#include <vector>

class Dictionary
{
public:
  typedef std::vector<std::pair<std::string, std::string>> Storage;

  bool hasKey(const std::string& key) const;
  void eraseKey(const std::string& key);

  const std::string& applyDefaultValue(const std::string& key,
                                       const std::string& value);
  const std::string& applyDefaultValue(const std::string& key, double value);

  double getNumber(const std::string& key) const;
  const std::string& getString(const std::string& key) const;

  const std::string& operator[](const std::string& key) const
  {
    return getString(key);
  }
  std::string& operator[](const std::string& key);

  Storage::const_iterator begin() const { return mData.begin(); }
  Storage::const_iterator end() const { return mData.end(); }
  bool empty() const { return mData.empty(); }

private:
  Storage mData;
};

#endif // DICTIONARY_H
