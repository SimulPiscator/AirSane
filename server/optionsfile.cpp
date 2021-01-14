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

#include "optionsfile.h"
#include "scanner.h"
#include <fstream>
#include <regex>
#include <sstream>

OptionsFile::OptionsFile(const std::string& fileName)
  : mFileName(fileName)
{
  std::ifstream file(fileName);
  if (file.is_open())
    std::clog << "reading device options from '" << fileName << "'"
              << std::endl;
  else
    std::clog << "no device options at '" << fileName << "'" << std::endl;

  std::string line;
  Options* pDeviceSection = nullptr;
  while (std::getline(file >> std::ws, line)) {

    if (line.empty() || line.front() == '#')
      continue;

    std::istringstream iss(line);
    std::string name, value;
    iss >> name;
    std::getline(iss >> std::ws, value);
    while (!value.empty() && std::isspace(value.back()))
      value.resize(value.length() - 1);
    if (name == "device") {
      mDeviceOptions.push_back(std::make_pair(value, Options()));
      pDeviceSection = &mDeviceOptions.back().second;
    } else if (pDeviceSection)
      pDeviceSection->push_back(std::make_pair(name, value));
    else
      mGlobalOptions.push_back(std::make_pair(name, value));
  }
}

OptionsFile::~OptionsFile() {}

std::string
OptionsFile::path() const
{
  size_t pos = mFileName.rfind('/');
  if (pos == std::string::npos)
    return "";
  return mFileName.substr(0, pos + 1);
}

OptionsFile::Options
OptionsFile::scannerOptions(const Scanner* pScanner) const
{
  auto options = mGlobalOptions;
  for (const auto& section : mDeviceOptions) {
    std::regex r(section.first);
    bool match = false;
    if (std::regex_match(pScanner->saneName(), r)) {
      std::clog << mFileName << ": regex '" << section.first
                << "' matches device name '" << pScanner->saneName() << "'"
                << std::endl;
      match = true;
    } else if (std::regex_match(pScanner->makeAndModel(), r)) {
      std::clog << mFileName << ": regex '" << section.first
                << "' matches device make and model '"
                << pScanner->makeAndModel() << "'" << std::endl;
      match = true;
    }
    if (match)
      options.insert(
        options.end(), section.second.begin(), section.second.end());
  }
  return options;
}
