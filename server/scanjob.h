/*
AirSane Imaging Daemon
Copyright (C) 2018 Simul Piscator

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

#ifndef SCANJOB_H
#define SCANJOB_H

#include <string>
#include "basic/dictionary.h"

class Scanner;

class ScanJob
{
public:
    ScanJob(const ScanJob&) = delete;
    ScanJob& operator=(const ScanJob&) = delete;

    ScanJob(Scanner*, const std::string& uuid);
    ~ScanJob();

    ScanJob& initWithScanSettingsXml(const std::string&, bool formatAutoselect = false);

    int ageSeconds() const;
    int imagesToTransfer() const;
    int imagesCompleted() const;
    std::string uri() const;
    const std::string& uuid() const;
    const std::string& documentFormat() const;
    void writeJobInfoXml(std::ostream&) const;

    bool beginTransfer();
    ScanJob& abortTransfer();
    ScanJob& finishTransfer(std::ostream&);
    ScanJob& cancel();

    typedef enum { aborted, canceled, completed, pending, processing } State;
    State state() const;
    std::string statusString() const;
    std::string statusReason() const;
    bool isPending() const;
    bool isProcessing() const;
    bool isFinished() const;
    bool isAborted() const;

private:
    struct Private;
    Private* p;
};

#endif // SCANJOB_H
