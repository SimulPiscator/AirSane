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
#include "workerthread.h"

#include <mutex>
#include <condition_variable>
#include <thread>
#include <iostream>
#include <cassert>

struct WorkerThread::Private
{
  std::mutex mMutex;
  std::condition_variable mThreadCondition, mExecuteCondition;
  Callable* mpCallable = nullptr;
  bool mCallDone = false;
  bool mStarted = false;
  bool mTerminate = false;

  std::thread mThread;
  void threadFunc();
};

WorkerThread::WorkerThread()
  : p(new Private)
{
  std::unique_lock<std::mutex> lock(p->mMutex);
  p->mStarted = false;
  p->mThread = std::thread([this](){ p->threadFunc(); });
  p->mThreadCondition.wait(lock, [this](){ return p->mStarted; });
}

WorkerThread::~WorkerThread()
{
  std::unique_lock<std::mutex> lock(p->mMutex);
  p->mTerminate = true;
  lock.unlock();
  p->mExecuteCondition.notify_one();
  if (p->mThread.joinable())
    p->mThread.join();
  delete p;
}

void WorkerThread::executeSynchronously(Callable& c)
{
  std::unique_lock<std::mutex> lock(p->mMutex);
  assert(p->mpCallable == nullptr);
  p->mpCallable = &c;
  p->mCallDone = false;
  p->mExecuteCondition.notify_one();
  p->mThreadCondition.wait(lock, [this](){ return p->mCallDone; });
}

void WorkerThread::Private::threadFunc()
{
  std::unique_lock<std::mutex> lock(mMutex);
  mStarted = true;
  lock.unlock();
  mThreadCondition.notify_one();
  while (!mTerminate)
  {
    std::unique_lock<std::mutex> lock(mMutex);
    mExecuteCondition.wait(lock, [this](){ return mTerminate || mpCallable; });
    if (mpCallable) {
      assert(!mCallDone);
      mpCallable->onCall();
      mpCallable = nullptr;
      mCallDone = true;
      lock.unlock();
      mThreadCondition.notify_one();
    }
  }
}
