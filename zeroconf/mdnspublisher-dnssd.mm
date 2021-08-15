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

#include "mdnspublisher.h"

#include <ctime>
#include <iostream>
#include <list>

#include <Foundation/Foundation.h>
#include <dns_sd.h>
#include <netdb.h>
#include <unistd.h>

namespace {

static struct
{
  int code;
  const char* text;
} sErrors[] = {
#define _(x) { x, #x },
  _(kDNSServiceErr_NoError)    //                    = 0,
  _(kDNSServiceErr_Unknown)    //                    = -65537,    // 0xFFFE FFFF
                               //                    (first error code)
  _(kDNSServiceErr_NoSuchName) //                    = -65538,
  _(kDNSServiceErr_NoMemory)   //                    = -65539,
  _(kDNSServiceErr_BadParam)   //                    = -65540,
  _(kDNSServiceErr_BadReference)   //                = -65541,
  _(kDNSServiceErr_BadState)       //                = -65542,
  _(kDNSServiceErr_BadFlags)       //                = -65543,
  _(kDNSServiceErr_Unsupported)    //                = -65544,
  _(kDNSServiceErr_NotInitialized) //                = -65545,
//  _(kDNSServiceErr_NoCache)         //             = -65546,
  _(kDNSServiceErr_AlreadyRegistered) //             = -65547,
  _(kDNSServiceErr_NameConflict)      //             = -65548,
  _(kDNSServiceErr_Invalid)           //             = -65549,
  _(kDNSServiceErr_Incompatible)      //             = -65551,
  _(kDNSServiceErr_BadInterfaceIndex) //             = -65552,
  _(kDNSServiceErr_Refused)           //             = -65553,
  _(kDNSServiceErr_NoSuchRecord)      //             = -65554,
  _(kDNSServiceErr_NoAuth)            //             = -65555,
  _(kDNSServiceErr_NoSuchKey)         //             = -65556,
//  _(kDNSServiceErr_NoValue)         //             = -65557,
//  _(kDNSServiceErr_BufferTooSmall)  //             = -65558,

// TCP Connection Status

//  _(kDNSServiceErr_ConnectionPending)     //       = -65570,
//  _(kDNSServiceErr_ConnectionFailed)      //       = -65571,
//  _(kDNSServiceErr_ConnectionEstablished) //       = -65572,

// Non-error values

//  _(kDNSServiceErr_GrowCache)             //       = -65790,
//  _(kDNSServiceErr_ConfigChanged)         //       = -65791,
//  _(kDNSServiceErr_MemFree)               //       = -65792    // 0xFFFE FF00
//  (last error code)
#undef _
  { 0, nullptr }
};
const char*
dnssd_strerr(int code)
{
  for (const auto err : sErrors)
    if (err.code == code)
      return err.text;
  return ::strerror(code);
}

struct ServiceEntry
{
  MdnsPublisher::Service* mpService;
  DNSServiceRef mDNSServiceRef;

  ServiceEntry(MdnsPublisher::Service* p)
    : mpService(p)
    , mDNSServiceRef(nullptr)
  {}

  ~ServiceEntry() { unannounce(); }

  bool announce()
  {
    unannounce();
    bool ok = true;
    TXTRecordRef txtRecord;
    ::TXTRecordCreate(&txtRecord, 0, nullptr);
    for (const auto& entry : mpService->txtRecord()) {
      DNSServiceErrorType err = ::TXTRecordSetValue(&txtRecord,
                                                    entry.first.c_str(),
                                                    entry.second.length(),
                                                    entry.second.data());
      if (err != kDNSServiceErr_NoError) {
        ok = false;
        std::cerr << "Could not add txtRecord value " << entry.first << "="
                  << entry.second << " (" << dnssd_strerr(err) << ")"
                  << std::endl;
      }
    }
    int ifindex = mpService->interfaceIndex();
    if (ifindex < 0)
      ifindex = 0;
    uint16_t port = mpService->port();
    DNSServiceErrorType err =
      ::DNSServiceRegister(&mDNSServiceRef,
                           0,
                           ifindex,
                           mpService->name().c_str(),
                           mpService->type().c_str(),
                           nullptr,
                           nullptr,
                           htons(port),
                           ::TXTRecordGetLength(&txtRecord),
                           ::TXTRecordGetBytesPtr(&txtRecord),
                           onRegisterService,
                           mpService);
    if (err != kDNSServiceErr_NoError) {
      ok = false;
      std::cerr << "Could not register service " << mpService->name() << " ("
                << dnssd_strerr(err) << ")" << std::endl;
    } else {
      int fd = ::DNSServiceRefSockFD(mDNSServiceRef);
      fd_set readfds;
      FD_ZERO(&readfds);
      FD_SET(fd, &readfds);
      int r = ::select(fd + 1, &readfds, nullptr, nullptr, nullptr);
      if (r == 1)
        ::DNSServiceProcessResult(mDNSServiceRef);
    }
    ::TXTRecordDeallocate(&txtRecord);
    return ok;
  }

  void unannounce()
  {
    if (mDNSServiceRef) {
      ::DNSServiceRefDeallocate(mDNSServiceRef);
      mDNSServiceRef = nullptr;
    }
  }

  static void onRegisterService(DNSServiceRef,
                                DNSServiceFlags,
                                DNSServiceErrorType errorCode,
                                const char* name,
                                const char* regType,
                                const char* domain,
                                void* data)
  {
    auto* pService = static_cast<MdnsPublisher::Service*>(data);
    if (errorCode == kDNSServiceErr_NoError)
      pService->setName(name);
  }
};

} // namespace

struct MdnsPublisher::Private
{
  std::string mHostnameFqdn, mHostname;
  std::list<ServiceEntry> mServices;

  std::list<ServiceEntry>::iterator findService(const Service* pService)
  {
    auto i = std::find_if(mServices.begin(),
                          mServices.end(),
                          [pService](const ServiceEntry& entry) {
                            return entry.mpService == pService;
                          });
    return i;
  }
};

MdnsPublisher::MdnsPublisher()
  : p(new Private)
{
  NSHost* host = [NSHost currentHost];
  auto names = [host names];
  if ([names count] > 0)
    p->mHostnameFqdn = [names[0] UTF8String];
  size_t pos = p->mHostnameFqdn.find('.');
  p->mHostname = p->mHostnameFqdn.substr(0, pos);
}

MdnsPublisher::~MdnsPublisher()
{
  delete p;
}

const std::string&
MdnsPublisher::hostname() const
{
  return p->mHostname;
}

const std::string&
MdnsPublisher::hostnameFqdn() const
{
  return p->mHostnameFqdn;
}

bool
MdnsPublisher::announce(MdnsPublisher::Service* pService)
{
  bool ok = false;
  auto i = p->findService(pService);
  if (i != p->mServices.end()) {
    ok = true;
  } else {
    p->mServices.push_back(ServiceEntry(pService));
    ok = p->mServices.back().announce();
  }
  return ok;
}

bool
MdnsPublisher::unannounce(MdnsPublisher::Service* pService)
{
  bool ok = false;
  auto i = p->findService(pService);
  if (i != p->mServices.end()) {
    ok = true;
    i->unannounce();
    p->mServices.erase(i);
  }
  return ok;
}
