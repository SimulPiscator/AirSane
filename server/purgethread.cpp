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

#include "purgethread.h"

#include <thread>
#include <unistd.h>
#include <sys/select.h>

struct PurgeThread::Private
{
  const ScannerList* mpScanners;
  std::thread* mpThread;
  int mWriteFd, mReadFd;
  int mSleepDuration, mMaxTime;

  void start();
  void terminate();
  void threadFunc();
  bool interruptibleSleep(int seconds);
};

void PurgeThread::Private::start()
{
  int fds[2] = {0};
  ::pipe(fds);
  mReadFd = fds[0];
  mWriteFd = fds[1];
  mpThread = new std::thread([this]{threadFunc();});
}

void PurgeThread::Private::terminate()
{
  if (mpThread->joinable()) {
    char c = 'x';
    ::write(mWriteFd, &c, 1);
    mpThread->join();
  }
  ::close(mReadFd);
  ::close(mWriteFd);
}

void PurgeThread::Private::threadFunc()
{
  while (interruptibleSleep(mSleepDuration)) {
    std::clog << "purging jobs with timeout of " << mMaxTime << "seconds";
    for (const auto& entry : *mpScanners)
      entry.pScanner->purgeJobs(mMaxTime);
  }
}

bool PurgeThread::Private::interruptibleSleep(int seconds)
{
  fd_set readSet;
  FD_ZERO(&readSet);
  FD_SET(mReadFd, &readSet);
  struct timeval timeout = { seconds, 0 };
  int count = 0;
  do {
    count = ::select(mReadFd + 1, &readSet, nullptr, nullptr, &timeout);
  } while (count < 0 && errno == EINTR);
  return count == 0;
}

PurgeThread::PurgeThread(const ScannerList& scanners, int sleepDuration, int maxTime)
: p(new Private)
{
  p->mpScanners = &scanners;
  p->mSleepDuration = sleepDuration;
  p->mMaxTime = maxTime;
  p->start();
}

PurgeThread::~PurgeThread()
{
  p->terminate();
  delete p;
}
