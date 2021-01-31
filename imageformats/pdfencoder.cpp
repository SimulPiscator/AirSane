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

#include "pdfencoder.h"

#include <arpa/inet.h>
#include <cassert>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <locale>
#include <stdexcept>
#include <vector>

#include "basic/dictionary.h"
#include "basic/uuid.h"

namespace {

struct smanip
{
  virtual ~smanip() {}
  virtual void insert(std::ostream&) const = 0;
};

std::ostream&
operator<<(std::ostream& os, const smanip& s)
{
  s.insert(os);
  return os;
}

std::string
pdfNameEncode(const std::string& s)
{
  std::string r;
  for (const auto c : s)
    switch (c) {
      case '\n':
      case '\r':
      case '\t':
      case ' ':
        r += "#20";
        break;
      default:
        r += c;
    }
  return r;
}

std::string
pdfStringEncode(const std::string& s)
{
  std::string r;
  for (const auto c : s)
    switch (c) {
      case '\n':
        r += "\\n";
        break;
      case '\t':
        r += "\\t";
        break;
      case '\r':
        r += "\\r";
        break;
      case '\b':
        r += "\\b";
        break;
      case '\f':
        r += "\\f";
        break;
      case '(':
      case ')':
      case '\\':
        r += '\\';
        // no break
      default:
        r += c;
    }
  return r;
}

std::string
pdfDate(::time_t t)
{
  struct tm tm;
  ::gmtime_r(&t, &tm);
  char buf[16];
  ::strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", &tm);
  return std::string("D:") + buf + "Z";
}

} // namespace

struct PdfEncoder::Private
{
  PdfEncoder* p;
  struct insert_objdef : smanip
  {
    Private* p;
    int id;
    insert_objdef(Private*, int);
    void insert(std::ostream&) const override;
  };
  insert_objdef defobj(int);

  struct objdef
  {
    int id;
    std::streamoff offset;
  };
  std::vector<objdef> mObjects;
  std::streampos mBegin;
  std::streamoff mStartxref;
  Dictionary mInfoDict;
  std::string mInfoString;

#if BYTE_ORDER == LITTLE_ENDIAN
  std::vector<uint16_t> mLineBuffer;
#endif

  Private(PdfEncoder*);
};

PdfEncoder::Private::insert_objdef
PdfEncoder::Private::defobj(int id)
{
  return insert_objdef(this, id);
}

PdfEncoder::Private::Private(PdfEncoder* p)
  : p(p)
  , mBegin(0)
  , mStartxref(0)
{}

PdfEncoder::PdfEncoder()
  : p(new Private(this))
{}

PdfEncoder::~PdfEncoder()
{
  delete p;
}

Dictionary&
PdfEncoder::documentInfo()
{
  return p->mInfoDict;
}

const Dictionary&
PdfEncoder::documentInfo() const
{
  return p->mInfoDict;
}

void
PdfEncoder::onImageBegin()
{
#if BYTE_ORDER == LITTLE_ENDIAN
  p->mLineBuffer.clear();
  if (bitDepth() == 16)
    p->mLineBuffer.resize(components() * width());
#endif
  p->mInfoDict["CreationDate"] = pdfDate(::time(nullptr));
  p->mInfoString.clear();
  for (const auto& entry : p->mInfoDict)
    if (!entry.first.empty() && !entry.second.empty())
      p->mInfoString += "/" + pdfNameEncode(entry.first) + " (" +
                        pdfStringEncode(entry.second) + ")\n";

  p->mObjects.clear();
  if (resolutionDpi() == 0)
    throw std::runtime_error("no resolution specified");
  const double pdfunits_per_px = 72.0 / resolutionDpi();
  const char* csname = "unknown";
  switch (components()) {
    case 1:
      csname = "DeviceGray";
      break;
    case 3:
      csname = "DeviceRGB";
      break;
    case 4:
      csname = "DeviceCMYK";
      break;
  };
  p->mBegin = destination()->tellp();
  destination()->imbue(std::locale("C"));
  *destination() << std::dec <<
    R"(%PDF-1.4
%âãÏÓ
)" << p->defobj(2)
                 << R"(
<<
/Type/Page
/Contents 4 0 R
/MediaBox [ 0 0 )"
                 << pdfunits_per_px * width() << " "
                 << pdfunits_per_px * height() << R"( ]
/Parent 1 0 R
/Resources << /XObject << /strip0 3 0 R >> >>
>>
endobj
)" << p->defobj(3)
                 << R"(
<<
/Type /XObject
/Subtype /Image
)"
                 << "/Width " << width() << " /Height " << height() << R"(
/ColorSpace /)" << csname
                 << R"(
/BitsPerComponent )"
                 << bitDepth() << R"(
/Length )" << height() * bytesPerLine()
                 << R"(
>>
stream
)";
}

void
PdfEncoder::onImageEnd()
{
  const double pdfunits_per_px = 72.0 / resolutionDpi();
  std::ostringstream oss;
  oss << pdfunits_per_px * width() << " 0 0 " << pdfunits_per_px * height()
      << " 0 0 cm\n"
      << "/strip0 Do\n";
  std::string pagedef = oss.str();
  std::string fileid = Uuid(p->mInfoString, ::time(nullptr)).toString();
  for (size_t pos = fileid.find('-'); pos < fileid.length();
       pos = fileid.find('-'))
    fileid = fileid.substr(0, pos) + fileid.substr(pos + 1);

  std::ostream& os = *destination();
  os <<
    R"(
endstream
endobj
)" << p->defobj(4)
     << R"(
<<
/Length )"
     << pagedef.length() << R"(
>>
)"
     << "stream\n"
     << pagedef << "endstream\n"
     << R"(
endobj
)" << p->defobj(1)
                 << R"(
<<
/Type/Pages
/Count 1
/Kids [ 2 0 R ]
>>
endobj
)" << p->defobj(5)
                 << R"(
<<
/Type/Catalog
/Pages 1 0 R
>>
endobj
)" << p->defobj(6)
                 << R"(
<<
)" << p->mInfoString
                 << R"(
>>
endobj
)";


  p->mStartxref = os.tellp() - p->mBegin;
  os << "xref\n"
     << "0 " << p->mObjects.size() + 1 << "\n"
     << "0000000000 65535 f \n";
  for (const auto& obj : p->mObjects)
    os << std::setfill('0') << std::setw(10) << obj.offset << " 00000 n \n";
  os <<
    R"(trailer
<<
/Size )"
     << p->mObjects.size() + 1 << R"(
/Root 5 0 R
/Info 6 0 R
/ID
[
<)" << fileid
     << ">\n<" << fileid << R"(>
]
>>
%PDF-raster-1.0
startxref
)" << p->mStartxref
     << R"(
%%EOF
)";
}

void
PdfEncoder::onWriteLine(const void* pReadFrom)
{
#if BYTE_ORDER == LITTLE_ENDIAN
  if (!p->mLineBuffer.empty()) {
    const uint16_t *pIn = static_cast<const uint16_t*>(pReadFrom),
                   *pEnd = pIn + p->mLineBuffer.size();
    uint16_t* pOut = p->mLineBuffer.data();
    while (pIn < pEnd)
      *pOut++ = htons(*pIn++);
    destination()->write(reinterpret_cast<const char*>(p->mLineBuffer.data()),
                         bytesPerLine());
    return;
  }
#endif
  destination()->write(static_cast<const char*>(pReadFrom), bytesPerLine());
}

PdfEncoder::Private::insert_objdef::insert_objdef(Private* p, int id)
  : p(p)
  , id(id)
{}

void
PdfEncoder::Private::insert_objdef::insert(std::ostream& os) const
{
  assert(id == p->mObjects.size() + 1);
  objdef obj = { id, os.tellp() - p->mBegin };
  os << id << " 0 obj";
  p->mObjects.push_back(obj);
}
