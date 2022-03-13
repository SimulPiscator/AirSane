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
  RawOptions* pDeviceSection = nullptr;
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
      mDeviceOptions.push_back(std::make_pair(value, RawOptions()));
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
  auto rawOptions = mGlobalOptions;
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
      rawOptions.insert(
        rawOptions.end(), section.second.begin(), section.second.end());
  }
  OptionsFile::Options processedOptions;
  for (const auto& option : rawOptions) {
    if (option.first == "icon") {
      processedOptions.icon = option.second;
      if (processedOptions.icon.find('/') != 0)
        processedOptions.icon = this->path() + processedOptions.icon;
    }
    else if (option.first == "note" || option.first == "location")
      processedOptions.note = option.second;
    else if (option.first == "gray-gamma")
      processedOptions.gray_gamma = ::atof(option.second.c_str());
    else if (option.first == "color-gamma")
      processedOptions.color_gamma = ::atof(option.second.c_str());
    else if (option.first == "synthesize-gray")
      processedOptions.synthesize_gray = (option.second == "true");
    else
      processedOptions.sane_options.push_back(option);
  }
  return processedOptions;
}
