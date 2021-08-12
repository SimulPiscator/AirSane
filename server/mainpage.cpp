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

#include "mainpage.h"

extern const char* GIT_COMMIT_HASH;
extern const char* GIT_BRANCH;
extern const char* GIT_REVISION_NUMBER;
extern const char* BUILD_TIME_STAMP;

MainPage::MainPage(const ScannerList& scanners, bool resetoption, bool discloseversion)
  : mScanners(scanners)
  , mResetoption(resetoption)
  , mDiscloseversion(discloseversion)
{}

void
MainPage::onRender()
{
  out() << heading(1).addText(title()) << std::endl;

  out() << heading(2).addText("Scanners");
  if (mScanners.empty()) {
    out() << paragraph().addText("No scanners available");
  } else {
    list scannersList;
    for (const auto& s : mScanners) {
      auto name = s.pScanner->publishedName();

      std::string icondef;
      if (!s.pScanner->iconUrl().empty() ) {
        icondef = "<img src='" + s.pScanner->iconUrl() + "'"
                + " alt='Scanner Icon'"
                + " style='width:1.2em;height:1.2em;vertical-align:bottom;padding-right:0.6em'"
                + ">";
      }
      scannersList.addItem(anchor(s.pScanner->adminUrl()).addContent(icondef).addText(name));
      scannersList.addContent("\n");
    }
    out() << scannersList << std::endl;
  }

  if (mDiscloseversion) {
    out() << heading(2).addText("Build");
    list version;
    version.addItem(
      paragraph().addText(std::string("date: ") + BUILD_TIME_STAMP));
    version.addContent("\n");
    version.addItem(paragraph().addText(
      std::string("commit: ") + GIT_COMMIT_HASH + " (branch " + GIT_BRANCH +
      ", revision " + GIT_REVISION_NUMBER + ")"));
    version.addContent("\n");
    out() << version << std::endl;
  }

  if (mResetoption) {
    out() << heading(2).addText("Server Maintenance");
    list maintenance;
    maintenance.addItem(anchor("/reset").addText("Reset"));
    maintenance.addContent("\n");
    out() << maintenance << std::endl;
  }
}
