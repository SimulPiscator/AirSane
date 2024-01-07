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

#include "web/httpserver.h"

#include <iostream>
#include <set>
#include <thread>

#include <poll.h>
#include <unistd.h>
#include <netinet/in.h>
#if __FreeBSD__
#include <netlink/netlink.h>
#include <netlink/netlink_route.h>
#else
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#endif
#include <net/if.h>
#include <ifaddrs.h>

#include <string.h>

namespace {
// Return true if a sorts smaller/less than b.
struct CompareAddresses
{
  bool operator()(const HttpServer::Sockaddr& a, const HttpServer::Sockaddr& b) const
  {
    if (a.sa.sa_family != b.sa.sa_family)
      return a.sa.sa_family < b.sa.sa_family;
    int cmp = 0;
    switch (a.sa.sa_family) {
    case AF_UNIX:
      cmp = ::strcmp(a.un.sun_path, b.un.sun_path);
      break;
    case AF_INET:
      cmp = ::memcmp(&a.in.sin_addr, &b.in.sin_addr, sizeof(a.in.sin_addr));
      break;
    case AF_INET6:
      cmp = ::memcmp(&a.in6.sin6_addr, &b.in6.sin6_addr, sizeof(a.in6.sin6_addr));
      break;
    }
    if (cmp < 0)
      return true;
    if (cmp > 0)
      return false;

    switch (a.sa.sa_family) {
    case AF_UNIX:
      return false;
    case AF_INET:
      cmp = ::memcmp(&a.in.sin_port, &b.in.sin_port, sizeof(a.in.sin_port));
      break;
    case AF_INET6:
      cmp = ::memcmp(&a.in6.sin6_port, &b.in6.sin6_port, sizeof(a.in6.sin6_port));
      break;
    }
    return cmp < 0;
  }
};

}

struct NetworkHotplugNotifier::Private
{
  std::thread mThread;
  NetworkHotplugNotifier* mpNotifier;
  int mPipeWriteFd, mPipeReadFd;

  std::set<HttpServer::Sockaddr, CompareAddresses> mAddresses;

  Private(NetworkHotplugNotifier* pNotifier)
  : mpNotifier(pNotifier), mPipeWriteFd(-1), mPipeReadFd(-1)
  {
    int fds[2];
    if (::pipe(fds) < 0) {
        std::cerr << "Could not create socket pair " 
                  << ::strerror(errno) << std::endl;
        return;
    }
    mPipeReadFd = fds[0];
    mPipeWriteFd = fds[1];
    mThread = std::thread([this]() { hotplugThread(); });
  }

  ~Private()
  {
    char c = '0';
    ::write(mPipeWriteFd, &c, 1);
    mThread.join();
    ::close(mPipeWriteFd);
    ::close(mPipeReadFd);
  }

  void initAddresses()
  {
    mAddresses.clear();
    ifaddrs* pAddrs = nullptr;
    int result = ::getifaddrs(&pAddrs);
    if (result < 0) {
      std::cerr << "Could not get addresses: " << errno << std::endl;
      return;
    }
    for (ifaddrs* p = pAddrs; p != nullptr; p = p->ifa_next) {
      HttpServer::Sockaddr address = {0};
      if (p->ifa_addr) {
        switch (p->ifa_addr->sa_family) {
        case AF_INET:
          ::memcpy(&address, p->ifa_addr, sizeof(address.in));
          break;
        case AF_INET6:
          ::memcpy(&address, p->ifa_addr, sizeof(address.in6));
          break;
        }
      }
      if (address.sa.sa_family != AF_UNSPEC)
        mAddresses.insert(address);
    }
    ::freeifaddrs(pAddrs);
  }

  void hotplugThread()
  {
    int sock = ::socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (sock < 0) {
      std::cerr << "Could not create netlink socket: "
                << ::strerror(errno) << std::endl;
      return;
    }

    sockaddr_nl addr = {0};
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR;
    if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
      std::cerr << "Could not bind netlink socket: " << errno << std::endl;
      ::close(sock);
      return;
    }

    initAddresses();

    struct pollfd pfds[2] = {0};
    pfds[0].fd = mPipeReadFd;
    pfds[0].events = POLLIN;
    pfds[1].fd = sock;
    pfds[1].events = POLLIN;

    char buffer[4096];
    bool done = false;
    while (!done) {
      int r = ::poll(pfds, sizeof(pfds)/sizeof(*pfds), -1);
      if (r > 0 && pfds[0].revents) {
        done = true;
      }
      else if (r > 0 && pfds[1].revents) {
        int len = ::read(sock, buffer, sizeof(buffer));
        if (len > 0) {
          union { const char* c; struct nlmsghdr* n; } data = { buffer };
          if (data.n->nlmsg_flags & MSG_TRUNC)
              continue;
          while (NLMSG_OK(data.n, len) && (data.n->nlmsg_type != NLMSG_DONE)) {
            if (data.n->nlmsg_type == RTM_NEWADDR
                || data.n->nlmsg_type == RTM_DELADDR
                || data.n->nlmsg_type == RTM_GETADDR) {
              struct ifaddrmsg* pIfa = static_cast<struct ifaddrmsg*>(NLMSG_DATA(data.n));
              int ifalen = IFA_PAYLOAD(data.n);
              struct rtattr* pRta = IFA_RTA(pIfa);
              HttpServer::Sockaddr address = {0};
              while (ifalen && RTA_OK(pRta, ifalen)) {
                if (pIfa->ifa_family == AF_INET && pRta->rta_type == IFA_ADDRESS) {
                  address.in.sin_family = AF_INET;
                  address.in.sin_addr = *reinterpret_cast<in_addr*>(RTA_DATA(pRta));
                }
                else if (pIfa->ifa_family == AF_INET6 && pRta->rta_type == IFA_ADDRESS) {
                  address.in6.sin6_family = AF_INET6;
                  address.in6.sin6_addr = *reinterpret_cast<in6_addr*>(RTA_DATA(pRta));
                }
                RTA_NEXT(pRta, ifalen);
              }
              if (address.sa.sa_family != AF_UNSPEC) {
                if (data.n->nlmsg_type == RTM_NEWADDR) {
                  if (mAddresses.find(address) == mAddresses.end()) {
                    mAddresses.insert(address);
                    std::clog << "New IP address: " << HttpServer::ipString(address) << std::endl;
                    mpNotifier->onHotplugEvent(addressArrived);
                  }
                }
                else if(data.n->nlmsg_type == RTM_DELADDR) {
                  mAddresses.erase(address);
                  std::clog << "IP address gone: " << HttpServer::ipString(address) << std::endl;
                  mpNotifier->onHotplugEvent(addressLeft);
                }
              }
            }
            NLMSG_NEXT(data.n, len);
          }
        }
      }
    }
  }
};

NetworkHotplugNotifier::NetworkHotplugNotifier()
  : p(new Private(this))
{}

NetworkHotplugNotifier::~NetworkHotplugNotifier()
{
  delete p;
}
