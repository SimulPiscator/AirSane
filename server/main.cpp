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

#include "server.h"
#include <csignal>
#include <thread>

static Server* pServer;

static void
onSignal(int signal)
{
  if (pServer)
    switch (signal) {
      case SIGHUP:
      case SIGTERM:
        pServer->terminate(signal);
        break;
    }
}

int
main(int argc, char** argv)
{
  Server server(argc, argv);
  pServer = &server;
  bool ok = true;
  struct sigaction action = { 0 };
  sigemptyset(&action.sa_mask);
  action.sa_handler = &onSignal;
  ::sigaction(SIGTERM, &action, nullptr);
  ::sigaction(SIGHUP, &action, nullptr);
  action.sa_handler = SIG_IGN;
  ::sigaction(SIGPIPE, &action, nullptr);
  auto serverThread = std::thread([&ok]() { ok = pServer->run(); });
  serverThread.join();
  pServer = nullptr;
  return ok ? 0 : -1;
}
