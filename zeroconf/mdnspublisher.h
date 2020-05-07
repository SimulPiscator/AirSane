/*
AirSane Imaging Daemon
Copyright (C) 2018-2020 Simul Piscator

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

#ifndef MDNSPUBLISHER_H
#define MDNSPUBLISHER_H

#include <string>
#include <vector>
#include <mutex>

class MdnsPublisher
{
    MdnsPublisher(const MdnsPublisher&) = delete;
    MdnsPublisher& operator=(const MdnsPublisher&) = delete;

public:
    MdnsPublisher();
    ~MdnsPublisher();

    class Service;
    bool announce(Service*);
    bool unannounce(Service*);

public:
    class Service
    {
    public:
        explicit Service(MdnsPublisher* p) : mpPublisher(p), mPort(0), mIfIndex(-1) {}
        ~Service() { unannounce(); }

        typedef std::vector<std::pair<std::string, std::string>> TxtRecord;

        Service& setType(const std::string& s) { mType = s; return *this; }
        const std::string& type() const { return mType; }
        Service& setName(const std::string&);
        std::string name() const;

        Service& setInterfaceIndex(int i) { mIfIndex = i; return *this; }
        int interfaceIndex() const { return mIfIndex; }
        Service& setPort(uint16_t p) { mPort = p; return *this; }
        uint16_t port() const { return mPort; }

        Service& setTxt(const std::string&, const std::string&);
        const std::string& txt(const std::string&) const;
        const TxtRecord& txtRecord() const { return mTxtRecord; }

        bool announce() { return mpPublisher->announce(this); }
        bool unannounce() { return mpPublisher->unannounce(this); }

    private:
        MdnsPublisher* mpPublisher;
        std::string mType, mName;
        int mIfIndex;
        uint16_t mPort;
        TxtRecord mTxtRecord;
        mutable std::mutex mNameMutex;
    };

private:
    struct Private;
    Private* p;
};

#endif // MDNSPUBLISHER_H
