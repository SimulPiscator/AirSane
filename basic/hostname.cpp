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

#include "hostname.h"

std::string hostname()
{
    std::string s = "<unknown>";
    char buf[256] = {0};
    if (::gethostname(buf, sizeof(buf) - 1) == 0) {
        s = buf;
    }
    return s;
}

std::string mHostNameFqdn()
{
    std::string s = hostname();
    struct addrinfo* info = nullptr;
    struct addrinfo hint = {0};
    hint.ai_flags = AI_CANONNAME;
    if (::getaddrinfo(s.c_str(), nullptr, &hint, &info) == 0) {
        s = info->ai_canonname;
        ::freeaddrinfo(info);
    }
    return s;
}
