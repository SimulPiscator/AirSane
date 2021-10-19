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

#include "pngencoder.h"
#ifdef __FreeBSD__
#include <png.h>
#else
#include <libpng16/png.h>
#endif
#include <stdexcept>
#include <vector>
#if __APPLE__
#include <machine/endian.h>
#elif __FreeBSD__
#include <sys/endian.h>
#else
#include <endian.h>
#endif
#include <arpa/inet.h>

struct PngEncoder::Private
{
  png_structp mpPng = nullptr;
  png_infop mpInfo = nullptr;
  std::ostream* mpStream = nullptr;
#if BYTE_ORDER == LITTLE_ENDIAN
  std::vector<uint16_t> mLineBuffer;
#endif

  static void write(png_structp png, png_bytep data, png_size_t size)
  {
    auto p = static_cast<Private*>(::png_get_io_ptr(png));
    p->mpStream->write(reinterpret_cast<const char*>(data), size);
  }

  static void flush(png_structp png)
  {
    auto p = static_cast<Private*>(::png_get_io_ptr(png));
    p->mpStream->flush();
  }

  [[noreturn]] static void error(png_structp, png_const_charp msg)
  {
    std::string message = "libpng error: ";
    message += msg;
    throw std::runtime_error(message);
  }

  static void warning(png_structp, png_const_charp msg)
  {
    std::clog << "libpng warning: " << msg << std::endl;
  }
};

PngEncoder::PngEncoder()
  : p(new Private)
{}

PngEncoder::~PngEncoder()
{
  delete p;
}

void
PngEncoder::onImageBegin()
{
  if (currentImage() > 0)
    throw std::runtime_error("PngEncoder: cannot encode more than one image per file");
  if (orientationDegrees() != 0)
    throw std::runtime_error("PngEncoder: cannot rotate image");
#if BYTE_ORDER == LITTLE_ENDIAN
  if (bitDepth() == 16)
    p->mLineBuffer.resize(width() * components());
  else
    p->mLineBuffer.clear();
#endif
  p->mpInfo = nullptr;
  p->mpPng =
    ::png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (p->mpPng) {
    p->mpInfo = ::png_create_info_struct(p->mpPng);
    ::png_set_write_fn(p->mpPng, p, &Private::write, &Private::flush);
    ::png_set_error_fn(p->mpPng, p, &Private::error, &Private::warning);
  }

  p->mpStream = destination();
  int colorType = 0;
  switch (colorspace()) {
    case Unknown:
      break;
    case Grayscale:
      colorType = PNG_COLOR_TYPE_GRAY;
      break;
    case RGB:
      colorType = PNG_COLOR_TYPE_RGB;
      break;
  }
  ::png_set_IHDR(p->mpPng,
                 p->mpInfo,
                 width(),
                 height(),
                 bitDepth(),
                 colorType,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE,
                 PNG_FILTER_TYPE_BASE);
  uint32_t px_per_m = resolutionDpi() * 10000 / 254;
  ::png_set_pHYs(p->mpPng, p->mpInfo, px_per_m, px_per_m, PNG_RESOLUTION_METER);
  ::png_write_info(p->mpPng, p->mpInfo);
  ::png_set_flush(p->mpPng, 10);
}

void
PngEncoder::onImageEnd()
{
  ::png_write_end(p->mpPng, nullptr);
  ::png_destroy_write_struct(&p->mpPng, &p->mpInfo);
  p->mpStream = nullptr;
}

void
PngEncoder::onWriteLine(const void* data)
{
#if BYTE_ORDER == LITTLE_ENDIAN
  if (p->mLineBuffer.empty())
    ::png_write_row(p->mpPng, static_cast<png_const_bytep>(data));
  else {
    const uint16_t* pData = static_cast<const uint16_t*>(data);
    for (size_t i = 0; i < p->mLineBuffer.size(); ++i)
      p->mLineBuffer[i] = htons(*pData++);
    ::png_write_row(p->mpPng,
                    reinterpret_cast<png_const_bytep>(p->mLineBuffer.data()));
  }
#else
  ::png_write_row(p->mpPng, static_cast<png_const_bytep>(data));
#endif
}
