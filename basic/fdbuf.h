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

#ifndef FDBUF_H
#define FDBUF_H

#include <streambuf>

class fdbuf : public std::streambuf
{
public:
    fdbuf(int fd, int putback = 1);
    ~fdbuf();
    int_type overflow(int_type c) override;
    int_type sync() override;
    int_type underflow() override;
    std::streampos seekoff(off_type, std::ios_base::seekdir, std::ios_base::openmode) override;

private:
    static const size_t bufsize = 4096;
    int mFd;
    int mPutback;
    std::streamsize mTotalWritten;
    char mOutbuf[bufsize], mInbuf[bufsize];
};

#endif // FDBUF_H
