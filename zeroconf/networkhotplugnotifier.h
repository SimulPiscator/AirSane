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

#ifndef NETWORK_HOTPLUGNOTIFIER_H
#define NETWORK_HOTPLUGNOTIFIER_H

class NetworkHotplugNotifier
{
  NetworkHotplugNotifier(const NetworkHotplugNotifier&) = delete;
  NetworkHotplugNotifier& operator=(const NetworkHotplugNotifier&) = delete;

public:
  NetworkHotplugNotifier();
  virtual ~NetworkHotplugNotifier();

protected:
  enum Event
  {
    other,
    addressArrived,
    addressLeft,
    addressChange,
  };
  virtual void onHotplugEvent(Event) {}

private:
  struct Private;
  Private* p;
};

#endif // NETWORK_HOTPLUGNOTIFIER_H
