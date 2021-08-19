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

#include "webpage.h"
#include <locale>
#include <sstream>

std::string
WebPage::htmlEscape(const std::string& s)
{
  std::string r;
  for (auto c : s)
    switch (c) {
      case '&':
        r += "&amp;";
        break;
      case '<':
        r += "&lt;";
        break;
      case '>':
        r += "&gt;";
        break;
      case '\'':
        r += "&apos;";
        break;
      case '"':
        r += "&quot;";
        break;
      case '\n':
        r += "<br>\n";
        break;
      default:
        r += c;
    }
  return r;
}

static std::locale clocale("C");

std::string
WebPage::numtostr(double d)
{
  std::ostringstream oss;
  oss.imbue(clocale);
  oss << d;
  return oss.str();
}

WebPage::WebPage()
  : mpResponse(nullptr)
  , mpRequest(nullptr)
  , mpOut(nullptr)
{
  addStyle("body { font-family:sans-serif }");
}

WebPage&
WebPage::setFavicon(const std::string& type, const std::string& url)
{
  mFaviconType = type;
  mFaviconUrl = url;
  return *this;
}

WebPage&
WebPage::clearFavicon()
{
  mFaviconType.clear();
  mFaviconUrl.clear();
  return *this;
}

WebPage&
WebPage::clearStyle()
{
  mStyle.clear();
  return *this;
}

WebPage&
WebPage::addStyle(const std::string& s)
{
  mStyle += s + "\n";
  return *this;
}

WebPage&
WebPage::render(const HttpServer::Request& request,
                HttpServer::Response& response)
{
  std::ostringstream oss;
  mpOut = &oss;
  mpRequest = &request;
  mpResponse = &response;
  onRender();
  mpResponse = nullptr;
  mpRequest = nullptr;
  mpOut = nullptr;
  if (!response.sent()) {
    std::string html = "<!DOCTYPE HTML>\n"
                       "<html>\n"
                       "<head>\n"
                       "<meta charset='utf-8'/>\n"
                       "<title>" +
                       htmlEscape(mTitle) +
                       "</title>\n"
                       "<style>" +
                       mStyle +
                       "</style>\n";

    if (!mFaviconType.empty() && !mFaviconUrl.empty())
      html +=          "<link rel='icon' type='" + mFaviconType + "' href='" + mFaviconUrl + "'>\n";

    html +=            "</head>\n"
                       "<body>\n";
    html += oss.str();
    html += "</body>\n</html>\n";
    response.setHeader(HttpServer::HTTP_HEADER_CONTENT_TYPE, "text/html");
    response.sendWithContent(html);
  }
  return *this;
}

WebPage::element&
WebPage::element::setAttribute(const std::string& key, const std::string& value)
{
  mAttributes[key] = value;
  return *this;
}

std::string
WebPage::element::toString() const
{
  std::string r = "<" + mTag;
  for (auto& a : mAttributes)
    r += " " + a.first + "='" + htmlEscape(a.second) + "'";
  r += ">";
  if (!mText.empty())
    r += mText + "</" + mTag + ">";
  return r;
}

WebPage::list&
WebPage::list::addItem(const std::string& s)
{
  addContent("<li>" + s + "</li>");
  return *this;
}

WebPage::list&
WebPage::list::addItem(const WebPage::element& el)
{
  return addItem(el.toString());
}

WebPage::formSelect&
WebPage::formSelect::addOption(const std::string& value,
                               const std::string& text)
{
  mOptions[value] = text.empty() ? value : text;
  return *this;
}

WebPage::formSelect&
WebPage::formSelect::addOptions(const std::vector<std::string>& options)
{
  for (auto& opt : options)
    addOption(opt);
  return *this;
}

std::string
WebPage::formSelect::toString() const
{
  std::string r = labelHtml();
  r += "<select autocomplete='off'";
  std::string value;
  for (auto& a : attributes()) {
    if (a.first == "value")
      value = a.second;
    else
      r += " " + a.first + "='" + a.second + "'";
  }
  r += ">\n";
  for (auto& opt : mOptions) {
    r += "<option value='" + opt.first + "'";
    if (opt.first == value)
      r += " selected";
    r += ">" + opt.second + "</option>\n";
  }
  r += "</select>\n";
  return r;
}

std::string
WebPage::formField::toString() const
{
  return labelHtml() + element::toString();
}

std::string
WebPage::formField::labelHtml() const
{
  std::string r;
  if (!mLabel.empty()) {
    const std::string& label = mLabel == "*" ? attributes()["name"] : mLabel;
    r += "<label for='" + attributes()["name"] + "'>" + label + "</label>\n";
  }
  return r;
}
