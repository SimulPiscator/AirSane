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

#include "networkhotplugnotifier.h"

#import <Foundation/Foundation.h>
#import <SystemConfiguration/SystemConfiguration.h>

#include <iostream>
#include <vector>
#include <thread>
#include <atomic>

#include <string.h>

struct NetworkHotplugNotifier::Private
{
  std::thread mThread;
  NetworkHotplugNotifier* mpNotifier;
  std::atomic<CFRunLoopRef> mRunLoop{NULL};
  std::atomic<bool> mTerminate{false};

  Private(NetworkHotplugNotifier* pNotifier)
  : mpNotifier(pNotifier)
  {
    mTerminate = false;
    mThread = std::thread([this]() { hotplugThread(); });
  }

  ~Private()
  {
    CFRunLoopRef runLoop = NULL;
    while (!runLoop)
      runLoop = mRunLoop;
    mTerminate = true;
    ::CFRunLoopStop(runLoop);
    mThread.join();
  }

  void hotplugThread()
  {
    mRunLoop = ::CFRunLoopGetCurrent();
    
    NSArray *SCMonitoringInterfacePatterns = @[@"State:/Network/Global/IPv[46]"];
    @autoreleasepool {
        SCDynamicStoreContext ctx = {0};
        ctx.info = this;
        ::SCDynamicStoreRef dsr = ::SCDynamicStoreCreate(NULL, CFSTR("network_interface_detector"), &notificationCallback, &ctx);
        ::SCDynamicStoreSetNotificationKeys(dsr, NULL, (CFArrayRef)::CFBridgingRetain(SCMonitoringInterfacePatterns));
        ::CFRunLoopAddSource(::CFRunLoopGetCurrent(), ::SCDynamicStoreCreateRunLoopSource(NULL, dsr, 0), kCFRunLoopDefaultMode);
        while (!mTerminate && [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate distantFuture]])
            ;
    }
  }
  
  static void notificationCallback(SCDynamicStoreRef store, CFArrayRef changedKeys, void*  info)
  {
#if 0
      @autoreleasepool {
          CFIndex count = CFArrayGetCount(changedKeys);
          for (CFIndex i = 0; i < count; ++i) {
            CFStringRef key = (CFStringRef)::CFArrayGetValueAtIndex(changedKeys, i);
            NSLog(@"Key \"%@\" was changed", key);
            CFPropertyListRef ref = ::SCDynamicStoreCopyValue(store, key);
            NSDictionary* dict = (NSDictionary*)::CFBridgingRelease(ref);
            NSLog(@"Value %@", dict);
          }
      }
#endif

      auto p = static_cast<Private*>(info);
      p->mpNotifier->onHotplugEvent(addressChange);
  }
};

NetworkHotplugNotifier::NetworkHotplugNotifier()
  : p(new Private(this))
{}

NetworkHotplugNotifier::~NetworkHotplugNotifier()
{
  delete p;
}
