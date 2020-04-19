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

#ifndef OPTIONS_FILE_H
#define OPTIONS_FILE_H

#include <string>
#include <vector>

class Scanner;

class OptionsFile
{
public:
    OptionsFile(const OptionsFile&) = delete;
    OptionsFile& operator=(const OptionsFile&) = delete;

    OptionsFile(const std::string&);
    ~OptionsFile();

    typedef std::vector<std::pair<std::string, std::string>> Options;
    Options scannerOptions(const Scanner*) const;

private:
    std::string mFileName;
    Options mGlobalOptions;
    std::vector<std::pair<std::string, Options>> mDeviceOptions;
};

#endif // OPTIONS_FILE_H
