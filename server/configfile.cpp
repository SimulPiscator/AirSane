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

#include "configfile.h"
#include "scanner.h"
#include <sstream>
#include <fstream>
#include <map>
#include <regex>

static ConfigFile::Section sEmptySection;

struct ConfigFile::Private
{
    std::string mFileName;
    Section mGlobalSection;
    std::map<std::string, Section> mDeviceSections;
};

ConfigFile::ConfigFile(const std::string& fileName)
: p(new Private)
{
    p->mFileName = fileName;
    std::ifstream file(fileName);
    std::string line;
    Section* pDeviceSection = nullptr;
    while(std::getline(file >> std::ws, line)) {
        std::istringstream iss(line);
        std::string name, value;
        iss >> name;
        std::getline(iss >> std::ws, value);
        while(!value.empty() && std::isspace(value.back()))
            value.resize(value.length() - 1);
        if(name == "device")
            pDeviceSection = &p->mDeviceSections[value];
        else if(!pDeviceSection)
            p->mGlobalSection.push_back(std::make_pair(name, value));
        else
            pDeviceSection->push_back(std::make_pair(name, value));
    }
}

ConfigFile::~ConfigFile()
{
    delete p;
}

const ConfigFile::Section& ConfigFile::globalSection() const
{
    return p->mGlobalSection;
}

const ConfigFile::Section& ConfigFile::deviceSection(const Scanner* pScanner) const
{
    for(const auto& section : p->mDeviceSections) {
        std::regex r(section.first);
        if(std::regex_match(pScanner->saneName(), r)) {
            std::clog << p->mFileName
                      << ": regex '" << section.first
                      << "' matches device name '"
                      << pScanner->saneName() << "'"
                      << std::endl;
            return section.second;
        }
        if(std::regex_match(pScanner->makeAndModel(), r)) {
            std::clog << p->mFileName
                      << ": regex '" << section.first
                      << "' matches device make and model '"
                      << pScanner->makeAndModel() << "'"
                      << std::endl;
            return section.second;
        }
    }
    return sEmptySection;
}
