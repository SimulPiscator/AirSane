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

#ifndef WEBPAGE_H
#define WEBPAGE_H

#include "httpserver.h"
#include "basic/dictionary.h"
#include <vector>
#include <string>
#include <iostream>

class WebPage
{
public:
    WebPage();
    virtual ~WebPage() {}

    WebPage& setTitle(const std::string& s) { mTitle = s; return *this; }
    const std::string& title() const { return mTitle; }

    WebPage& addStyle(const std::string&);
    const std::string& style() const { return mStyle; }
    WebPage& clearStyle();

    WebPage& render(const HttpServer::Request&, HttpServer::Response&);

    static std::string htmlEscape(const std::string&);
    static std::string numtostr(double);

    class element
    {
    public:
        element(const std::string& tag) : mTag(tag) {}
        virtual ~element() {}
        element& addText(const std::string& s) { return addContent(htmlEscape(s)); }
        element& addText(double d) { return addText(numtostr(d)); }
        element& addContent(const std::string& s) { mText += s; return *this; }
        element& setAttribute(const std::string&, const std::string&);
        element& setAttribute(const std::string& s, double d) { return setAttribute(s, numtostr(d)); }

        const Dictionary& attributes() const { return mAttributes; }

        operator std::string() const { return toString(); }
        virtual std::string toString() const;

    private:
        Dictionary mAttributes;
        std::string mTag, mText;
    };
    struct br : element
    {
        br() : element("br") {}
        std::string toString() const override { return element::toString() + "\n"; }
    };
    struct heading : element
    {
        heading(int level) : element("h" + numtostr(level)) {}
    };
    struct paragraph : element
    {
        paragraph() : element("p") {}
        std::string toString() const override { return element::toString() + "\n"; }
    };
    struct list : element
    {
        list() : element("ul") {}
        list& addItem(const std::string&);
    };
    struct anchor : element
    {
        anchor(const std::string& href = "") : element("a") { setAttribute("href", href); }
    };
    struct formField : element
    {
        formField(const std::string& tag) : element(tag) {}
        formField& setName(const std::string& s) { setAttribute("name", s); return *this; }
        formField& setValue(const std::string& s) { setAttribute("value", s); return *this; }
        formField& setLabel(const std::string& s) { mLabel = s; return *this; }
        std::string toString() const override;
        std::string labelHtml() const;

    private:
        std::string mLabel;
    };
    struct formInput : formField
    {
        formInput(const std::string& type) : formField("input") { setAttribute("type", type); }
    };
    struct formSelect : formField
    {
        formSelect() : formField("select") {}
        formSelect& addOption(const std::string& value, const std::string& text = "");
        formSelect& addOptions(const Dictionary&);
        formSelect& addOptions(const std::vector<std::string>&);
        std::string toString() const override;
    private:
        Dictionary mOptions;
    };

protected:
    virtual void onRender() = 0;
    std::ostream& out() const { return *mpOut; }
    const HttpServer::Request& request() const { return *mpRequest; }
    HttpServer::Response& response() const { return *mpResponse; }

private:
    std::string mTitle, mStyle;
    std::ostream* mpOut;
    const HttpServer::Request* mpRequest;
    HttpServer::Response* mpResponse;
};

inline std::ostream& operator<<(std::ostream& os, const WebPage::element& el)
{ return os << el.operator std::string(); }

#endif // WEBPAGE_H
