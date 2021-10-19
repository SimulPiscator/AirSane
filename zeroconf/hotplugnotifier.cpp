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

#include "hotplugnotifier.h"

#include <atomic>
#include <csignal>
#ifdef __FreeBSD__
#include <libusb.h>
#else
#include <libusb-1.0/libusb.h>
#endif
#include <pthread.h>
#include <thread>

struct HotplugNotifier::Private
{
  libusb_context* mpContext;
  std::thread mThread;
  ::libusb_hotplug_callback_handle mCbHandle;
  std::atomic<bool> mTerminate;

  Private(HotplugNotifier* pNotifier)
    : mpContext(nullptr)
    , mCbHandle(0)
    , mTerminate(false)
  {
    ::libusb_init(&mpContext);
    int events =
      LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED | LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT;
    ::libusb_hotplug_register_callback(mpContext,
                                       libusb_hotplug_event(events),
                                       LIBUSB_HOTPLUG_NO_FLAGS,
                                       LIBUSB_HOTPLUG_MATCH_ANY,
                                       LIBUSB_HOTPLUG_MATCH_ANY,
                                       LIBUSB_HOTPLUG_MATCH_ANY,
                                       &Private::hotplugCallback,
                                       pNotifier,
                                       &mCbHandle);
    mThread = std::thread([this]() { hotplugThread(); });
  }

  ~Private()
  {
    mTerminate = true;
    if (mCbHandle)
      ::libusb_hotplug_deregister_callback(mpContext, mCbHandle);
    mThread.join();
    ::libusb_exit(mpContext);
  }

  void hotplugThread()
  {
    int err = 0;
    while (err == 0) {
      if (mTerminate)
        return;
      err = ::libusb_handle_events(mpContext);
      switch (err) {
        case LIBUSB_ERROR_INTERRUPTED:
        case LIBUSB_ERROR_TIMEOUT:
          err = 0;
          break;
      }
    }
  }

  static int hotplugCallback(libusb_context*,
                             libusb_device*,
                             libusb_hotplug_event libusbevent,
                             void* p)
  {
    Event event = other;
    switch (libusbevent) {
      case LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED:
        event = deviceArrived;
        break;
      case LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT:
        event = deviceLeft;
        break;
    }
    static_cast<HotplugNotifier*>(p)->onHotplugEvent(event);
    return 0;
  }
};

HotplugNotifier::HotplugNotifier()
  : p(new Private(this))
{}

HotplugNotifier::~HotplugNotifier()
{
  delete p;
}
