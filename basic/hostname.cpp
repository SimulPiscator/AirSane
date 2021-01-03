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

#include <iostream>
#include <cerrno>
#include <cstring>

#include <unistd.h>
#include <netdb.h>

std::string hostname()
{
    std::string s = "<unknown>";
    char buf[256] = {0};
    if (::gethostname(buf, sizeof(buf) - 1) == 0) {
        s = buf;
    }
    else {
        std::cerr << "gethostname() error: "
                  << ::strerror(errno)
                  << std::endl;
    }
    return s;
}

std::string hostnameFqdn()
{
    std::string s = hostname();
    struct addrinfo* info = nullptr;
    int err = ::getaddrinfo(s.c_str(), nullptr, nullptr, &info);
    if (err) {
        std::cerr << "getaddrinfo() error: "
                  << ::gai_strerror(err)
                  << std::endl;
    }
    else {
        char node[NI_MAXHOST];
        err = ::getnameinfo(info->ai_addr, info->ai_addrlen, node, sizeof(node), nullptr, 0, 0);
        if (err) {
            std::cerr << "getnameinfo() error: "
                      << ::gai_strerror(err) 
                      << std::endl;
        }
        else {
            s = node;
        }
        ::freeaddrinfo(info);
    }
    return s;
}
