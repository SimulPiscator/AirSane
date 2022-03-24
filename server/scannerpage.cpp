/*
AirSane Imaging Daemon
Copyright (C) 2018-2022 Simul Piscator

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

#include "scannerpage.h"
#include "scanjob.h"
#include "scanner.h"

namespace {

static const struct
{
  const char* name;
  int widthPx300dpi, heightPx300dpi;
} paperSizes[] = {
  { "A4 Portrait", 2480, 3508 },
  { "A4 Landscape", 3508, 2480 },
  { "A5 Portrait", 1748, 2480 },
  { "A5 Landscape", 2480, 1748 },
  { "A6 Portrait", 1240, 1748 },
  { "A6 Landscape", 1748, 1240 },
  { "US Letter", 2550, 3300 },
  { "US Legal", 2550, 4200 },
  { "Full", 0, 0 },
};

std::string
buildScanJobTicket(const Dictionary& dict)
{
  Dictionary d = dict;
  d.eraseKey("preview");
  d.eraseKey("Resolution");
  d["XResolution"] = dict["Resolution"];
  d["YResolution"] = dict["Resolution"];
  d.eraseKey("PaperSize");
  d["XOffset"] = "0";
  d["YOffset"] = "0";
  d["BatchIfPossible"] = "1";
  std::string ticket = "<x:ContentRegionUnits>escl:ThreeHundredthsOfInches</"
                       "x:ContentRegionUnits>\n";
  for (auto& s : d) // just enough xml syntax for ScanJob to recognize
    ticket += "<x:" + s.first + ">" + s.second + "</x:" + s.first + ">\n";
  return ticket;
}

}

ScannerPage::ScannerPage(Scanner& s)
  : mScanner(s)
{
  setFavicon(HttpServer::MIME_TYPE_PNG, mScanner.iconUrl());
}

void
ScannerPage::onRender()
{
  std::string imageuri, statusinfo;

  Dictionary d = request().formData();
  bool preview = d.hasKey("preview"), download = d.hasKey("download");
  if (preview || download) {
    Dictionary scandict = d;
    if (preview) {
      int res = std::max(50, mScanner.minResDpi());
      scandict["Resolution"] = numtostr(res);
      scandict["Intent"] = "Preview";
      scandict["PaperSize"] = "Full";
      scandict["DocumentFormat"] = HttpServer::MIME_TYPE_JPEG;
      scandict["ColorMode"] = "RGB24";
    } else {
      scandict["Intent"] = "TextAndGraphic";
    }
    for (auto& paper : paperSizes)
      if (scandict["PaperSize"] == paper.name) {
        int width = paper.widthPx300dpi, height = paper.heightPx300dpi;
        if (width == 0)
          width = mScanner.maxWidthPx300dpi();
        if (height == 0)
          height = mScanner.maxHeightPx300dpi();
        scandict["Width"] = numtostr(width);
        scandict["Height"] = numtostr(height);
        break;
      }
    auto ticket = buildScanJobTicket(scandict);
    auto job = mScanner.createJobFromScanSettingsXml(ticket);
    if (job && download) {
      if (job->beginTransfer()) {
        auto& format = job->documentFormat();
        std::string filename = "Scan" + HttpServer::fileExtension(format);
        response().setHeader(HttpServer::HTTP_HEADER_CONTENT_DISPOSITION,
                             "attachment;filename=\"" + filename + "\"");
        response().setHeader(HttpServer::HTTP_HEADER_CONTENT_TYPE, format);
        response().setHeader(HttpServer::HTTP_HEADER_TRANSFER_ENCODING,
                             "chunked");
        job->finishTransfer(response().send());
        return;
      } else {
        statusinfo =
          "Error: " + job->statusString() + ": " + job->statusReason();
        response().setHeader(HttpServer::HTTP_HEADER_REFRESH, "5; url=/");
      }
    }
    if (job && preview)
      imageuri = job->uri() + "/NextDocument";
  }

  std::string icondef;
  if (!mScanner.iconUrl().empty() ) {
    icondef = "<img src='" + mScanner.iconUrl() + "'"
            + " alt='Scanner Icon'"
            + " style='width:1.2em;height:1.2em;vertical-align:bottom;padding-right:0.3em'"
            + ">";
  }

  if (!title().empty())
    out() << heading(1).addContent(icondef).addText(title());

  const struct
  {
    const char *key, *value;
  } defaults[] = {
    { "InputSource", "Platen" },
    { "DocumentFormat", "image/jpeg" },
    { "Resolution", "300 dpi" },
  };
  for (auto& f : defaults)
    d.applyDefaultValue(f.key, f.value);
  d.applyDefaultValue("ColorMode", mScanner.colorModes().front());

  std::vector<std::string> resolutions;
  resolutions.push_back(numtostr(mScanner.minResDpi()) + " dpi");
  for (auto r : { 300, 600, 1200 })
    if (mScanner.maxResDpi() >= r)
      resolutions.push_back(numtostr(r) + " dpi");

  std::vector<std::string> papers;
  for (auto& paper : paperSizes)
    if (paper.widthPx300dpi <= mScanner.maxWidthPx300dpi() &&
        paper.heightPx300dpi <= mScanner.maxHeightPx300dpi())
      papers.push_back(paper.name);
  d.applyDefaultValue("PaperSize", papers.front());

  out() << "<form id='scanform' method='POST'>\n"
        << "<div id='maindiv'>\n"
        << "<div id='settings'>\n";

  std::string note;
  for (const auto& s : mScanner.inputSources())
      if (s == "Feeder")
          note = "Choose \"Feeder\" input source and \"PDF\"<br>document type to scan multiple pages.";

  const struct
  {
    const char *name, *label;
    const std::vector<std::string>& options;
  } select[] = {
    { "DocumentFormat", "Document type", mScanner.documentFormats() },
    { "ColorMode", "Color mode", mScanner.colorModes() },
    { "InputSource", "Input source", mScanner.inputSources() },
    { "PaperSize", "Paper size", papers },
    { "Resolution", "Resolution", resolutions },
  };
  for (auto& s : select)
    out() << "<nobr>"
          << formSelect()
               .addOptions(s.options)
               .setName(s.name)
               .setLabel(s.label)
               .setValue(d[s.name])
          << "</nobr>" << br();
  out() << "<div id='note'>" << note << "</div>\n";
  out() << "<div id='status'>" << statusinfo << "</div>\n";
  out() << "</div>\n"
        << "<div id='downloadbtn'>\n"
        << formInput("submit").setName("download").setValue("Scan and download")
        << "</div>\n";
  int imgwidth = 320, imgheight = mScanner.maxHeightPx300dpi() * imgwidth /
                                  mScanner.maxWidthPx300dpi();
  std::string s = "width:" + numtostr(imgwidth) +
                  "px;"
                  "height:" +
                  numtostr(imgheight) + "px";
  out() << "<div id='previewpane'>"
        << "<div id='previewimg' style='" << s << "'>\n";
  if (imageuri.empty())
    out()
      << element("span").setAttribute("id", "previewlabel").addText("Preview");
  else
    out() << element("img")
               .setAttribute("src", imageuri)
               .setAttribute("alt", "Preview")
               .setAttribute("width", imgwidth)
               .setAttribute("height", imgheight);
  out() << "</div>\n"
        << formInput("submit")
             .setName("preview")
             .setValue("Update preview")
             .setAttribute("id", "previewbtn")
        << "</div>\n"
        << "</div>\n"
        << "</form>\n";

  addStyle(R"(
        #scanform { position:relative; float:left; overflow:hidden; background-color:lightsteelblue }
        #maindiv { float:left; overflow:hidden; padding:0 }
        #settings { float:left; min-width:45%; padding:0.2em }
        #downloadbtn { position:absolute; bottom:8px; margin-left:8px }
        #previewbtn  { position:absolute; bottom:8px; margin-left:8px }
        #note { padding:2em; font-size:small }
        #status { padding-top:2em; color:red }
        #previewpane { overflow:hidden }
        #previewimg { background-color:lightgray; line-height:2.5em; text-align:left }
        #previewlabel { position:absolute; top 8px; margin-left:8px }
        label { display:inline-block; padding-right:5%; padding-top:0.5em; width:40%; text-align:right; }
        input[type=text], input[type=number] { display:inline-block; width:40%; text-align:left; }
    )");
}
