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

#include "fdbuf.h"
#include <cassert>
#include <cstring>
#include <sys/ioctl.h>
#include <unistd.h>

fdbuf::fdbuf(int fd, int putback)
  : mFd(fd)
  , mPutback(putback)
  , mTotalWritten(0)
{
  assert(mPutback < sizeof(mInbuf));
  setp(mOutbuf, mOutbuf + sizeof(mOutbuf) - 1);
  setg(nullptr, nullptr, nullptr);
}

fdbuf::~fdbuf()
{
  fdbuf::sync();
  ::close(mFd);
}

fdbuf::int_type
fdbuf::overflow(int_type c)
{
  if (c != traits_type::eof()) {
    *pptr() = char(c);
    pbump(1);
    if (sync() == 0)
      return c;
  }
  return traits_type::eof();
}

fdbuf::int_type
fdbuf::sync()
{
  auto n = pptr() - pbase();
  pbump(-n);
  const char* p = pbase();
  while (n > 0) {
    int written = ::write(mFd, p, n);
    if (written < 0) {
      if (errno != EINTR)
        return -1;
    }
    else { // written >= 0
      n -= written;
      p += written;
      mTotalWritten += written;
    }
  }
  return 0;
}

fdbuf::int_type
fdbuf::underflow()
{
  if (gptr() >= egptr()) {
    char *start = mInbuf, *endbuf = mInbuf + sizeof(mInbuf);
    if (eback() == mInbuf) { // not first call
      ::memmove(mInbuf, gptr() - mPutback, mPutback);
      start = mInbuf + mPutback;
    }
    assert(start < endbuf);

    int n;
    if (!::ioctl(mFd, FIONREAD, &n)) {
      n = std::min<int>(n, endbuf - start);
      n = std::max(n, 1);
    } else if (errno == ENOTTY)
      n = endbuf - start;
    else
      return traits_type::eof();

    int read = ::read(mFd, start, n);
    if (read == 0 || (read < 0 && errno != EINTR))
      return traits_type::eof();
    if (read > 0)
      setg(mInbuf, start, start + read);
  }
  return traits_type::to_int_type(*gptr());
}

std::streampos
fdbuf::seekoff(std::streambuf::off_type offset,
               std::ios_base::seekdir dir,
               std::ios_base::openmode mode)
{
  if (offset == 0 && dir == std::ios_base::cur && mode == std::ios_base::out)
    return mTotalWritten + pptr() - mOutbuf;
  return -1;
}
