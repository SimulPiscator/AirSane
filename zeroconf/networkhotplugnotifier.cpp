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

#include <iostream>
#include <vector>
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

#include <string.h>

struct NetworkHotplugNotifier::Private
{
  std::thread mThread;
  NetworkHotplugNotifier* mpNotifier;
  int mPipeWriteFd, mPipeReadFd;

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
              if (data.n->nlmsg_type == RTM_NEWADDR)
                  mpNotifier->onHotplugEvent(addressArrived);
              else if(data.n->nlmsg_type == RTM_DELADDR)
                  mpNotifier->onHotplugEvent(addressLeft);
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
