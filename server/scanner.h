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

#ifndef SCANNER_H
#define SCANNER_H

#include <iostream>
#include <memory>
#include <string>

#include "optionsfile.h"
#include "sanecpp/sanecpp.h"

class ScanJob;

class Scanner
{
  Scanner(const Scanner&) = delete;
  Scanner& operator=(const Scanner&) = delete;

public:
  explicit Scanner(const sanecpp::device_info&, bool randomUuid = false);
  ~Scanner();

  const char* error() const;
  std::string statusString() const;

  const std::string& uuid() const;
  const std::string& makeAndModel() const;
  const std::string& saneName() const;
  const std::string& stableUniqueName() const;

  void setPublishedName(const std::string&);
  const std::string& publishedName() const;

  void setUri(const std::string&);
  const std::string& uri() const;
  void setAdminUrl(const std::string&);
  const std::string& adminUrl() const;
  void setIconUrl(const std::string&);
  const std::string& iconUrl() const;
  const std::string& iconFile() const;

  const std::vector<std::string>& documentFormats() const;
  const std::vector<std::string>& txtColorSpaces() const;
  const std::vector<std::string>& colorModes() const;
  std::vector<std::string> platenSupportedIntents() const;
  std::vector<std::string> adfSupportedIntents() const;
  const std::vector<std::string>& inputSources() const;

  int minResDpi() const;
  int maxResDpi() const;
  int maxWidthPx300dpi() const;
  int maxHeightPx300dpi() const;

  bool hasPlaten() const;
  bool hasAdf() const;
  bool hasDuplexAdf() const;

  std::string platenSourceName() const;
  std::string adfSourceName() const;
  std::string grayScanModeName() const;
  std::string colorScanModeName() const;

  std::shared_ptr<ScanJob> createJobFromScanSettingsXml(
    const std::string&,
    bool autoselectFormat = false);
  std::shared_ptr<ScanJob> getJob(const std::string& uuid);
  bool cancelJob(const std::string&);
  int purgeJobs(int maxAgeSeconds);
  typedef std::vector<std::shared_ptr<ScanJob>> JobList;
  JobList jobs() const;
  void setTemporaryAdfStatus(SANE_Status);

  std::shared_ptr<sanecpp::session> open();
  bool isOpen() const;

  void setDeviceOptions(const OptionsFile&);

  void writeScannerCapabilitiesXml(std::ostream&) const;
  void writeScannerStatusXml(std::ostream&) const;

private:
  struct Private;
  Private* p;
};

#endif // SCANNER_H
