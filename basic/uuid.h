/*
AirSane Imaging Daemon
Copyright (C) 2018-2020 Simul Piscator

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

#ifndef UUID_H
#define UUID_H

#include <string>
#include <sstream>

class Uuid
{
public:
    Uuid();
    Uuid(const std::string& s) { initFromString(s); }
    template<typename... Args> Uuid(Args... args) { initFromString(makeString(args...)); }

    std::ostream& print(std::ostream&) const;
    operator std::string() const;

    size_t size() const;
    char* data();
    const char* data() const;

private:
    void initFromString(const std::string&);
    static const std::string& makeString(const std::string& s) { return s; }
    template<typename T> static std::string makeString(const T& t)
        { std::ostringstream oss; oss << t; return oss.str(); }
    template<typename T, typename... Args> static std::string makeString(const T& t, Args... args)
        { return makeString(t) + makeString(args...); }

    char mData[16];
};

#endif // UUID_H
