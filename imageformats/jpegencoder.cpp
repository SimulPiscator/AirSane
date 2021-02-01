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

#include "jpegencoder.h"
#include <cstring>
#include <jerror.h>
#include <jpeglib.h>
#include <stdexcept>
#include <vector>

static_assert(BITS_IN_JSAMPLE == 8,
              "libjpeg must have been compiled with BITS_IN_JSAMPLE = 8");

namespace {

struct OstreamDestinationMgr : ::jpeg_destination_mgr
{
  JOCTET buffer[4096];
  std::ostream* os;

  static void init_destination_(::jpeg_compress_struct* p)
  {
    reinterpret_cast<OstreamDestinationMgr*>(p->dest)->init();
  }

  static void term_destination_(::jpeg_compress_struct* p)
  {
    reinterpret_cast<OstreamDestinationMgr*>(p->dest)->term();
  }

  static boolean empty_output_buffer_(::jpeg_compress_struct* p)
  {
    return reinterpret_cast<OstreamDestinationMgr*>(p->dest)->empty_buffer();
  }

  OstreamDestinationMgr()
    : os(nullptr)
  {
    init_destination = &init_destination_;
    term_destination = &term_destination_;
    empty_output_buffer = &empty_output_buffer_;
  }

  void init()
  {
    free_in_buffer = sizeof(buffer);
    next_output_byte = buffer;
  }

  void term()
  {
    if (os) {
      const char* p = reinterpret_cast<const char*>(buffer);
      os->write(p, sizeof(buffer) - free_in_buffer);
    }
  }

  boolean empty_buffer()
  {
    free_in_buffer = 0; // not updated by libjpeg-turbo
    next_output_byte = buffer + sizeof(buffer);
    term();
    init();
    return TRUE;
  }
};

} // namespace

struct JpegEncoder::Private
{
  ::jpeg_compress_struct mCompressStruct;
  ::jpeg_error_mgr mErrorMgr;
  OstreamDestinationMgr mDestMgr;

  int mQualityPercent;
  bool mCompressing;

  [[noreturn]] static void throwOnError(::jpeg_common_struct* p)
  {
    char buf[JMSG_LENGTH_MAX];
    (p->err->format_message)(p, buf);
    ::jpeg_abort(p);
    std::string msg = "libjpeg error: ";
    msg += buf;
    throw std::runtime_error(msg);
  }

  Private()
    : mQualityPercent(90)
    , mCompressing(false)
  {
    ::jpeg_std_error(&mErrorMgr);
    mErrorMgr.error_exit = &throwOnError;
    mCompressStruct.err = &mErrorMgr;
    ::jpeg_create_compress(&mCompressStruct);
    mCompressStruct.in_color_space = JCS_RGB;
  }
  ~Private()
  {
    if (mCompressing)
      try {
        ::jpeg_abort_compress(&mCompressStruct);
      } catch (const std::runtime_error&) {
        if (mErrorMgr.emit_message)
          mErrorMgr.emit_message(j_common_ptr(&mCompressStruct), -1);
      }
    ::jpeg_destroy_compress(&mCompressStruct);
  }
};

JpegEncoder::JpegEncoder()
  : p(new Private)
{}

JpegEncoder::~JpegEncoder()
{
  delete p;
}

JpegEncoder&
JpegEncoder::setGamma(double d)
{
  onParamChange();
  p->mCompressStruct.input_gamma = d;
  return *this;
}

double
JpegEncoder::gamma() const
{
  return p->mCompressStruct.input_gamma;
}

JpegEncoder&
JpegEncoder::setQualityPercent(int i)
{
  onParamChange();
  if (i < 0 || i > 100)
    throw std::runtime_error(
      "JpegEncoder: qualityPercent outside 0..100 range");
  p->mQualityPercent = i;
  return *this;
}

int
JpegEncoder::qualityPercent() const
{
  return p->mQualityPercent;
}

void
JpegEncoder::onImageBegin()
{
  if (bitDepth() != BITS_IN_JSAMPLE)
    throw std::runtime_error("JpegEncoder: bit depth unsupported");
  if (currentImage() > 0)
    throw std::runtime_error("JpegEncoder: cannot encode more than one image per file");
  if (orientationDegrees() != 0)
    throw std::runtime_error("JpegEncoder: cannot rotate image");
  p->mCompressStruct.image_width = width();
  p->mCompressStruct.image_height = height();
  p->mCompressStruct.input_components = components();
  switch (colorspace()) {
    case Grayscale:
      p->mCompressStruct.in_color_space = JCS_GRAYSCALE;
      break;
    case RGB:
      p->mCompressStruct.in_color_space = JCS_RGB;
      break;
    default:
      p->mCompressStruct.in_color_space = JCS_UNKNOWN;
  }
  p->mDestMgr.os = destination();
  ::jpeg_set_defaults(&p->mCompressStruct);
  ::jpeg_set_quality(&p->mCompressStruct, p->mQualityPercent, TRUE);
  if (p->mQualityPercent == 100) {
    p->mCompressStruct.smoothing_factor = 0;
    if (p->mCompressStruct.dct_method == JDCT_IFAST)
      p->mCompressStruct.dct_method = JDCT_ISLOW;
    p->mCompressStruct.comp_info[0].h_samp_factor = 1;
    p->mCompressStruct.comp_info[0].v_samp_factor = 1;
    p->mCompressStruct.comp_info[1].h_samp_factor = 1;
    p->mCompressStruct.comp_info[1].v_samp_factor = 1;
    p->mCompressStruct.comp_info[2].h_samp_factor = 1;
    p->mCompressStruct.comp_info[2].v_samp_factor = 1;
  }
  p->mCompressStruct.density_unit = 1;
  p->mCompressStruct.X_density = resolutionDpi();
  p->mCompressStruct.Y_density = resolutionDpi();
  p->mCompressStruct.dest = &p->mDestMgr;
  ::jpeg_start_compress(&p->mCompressStruct, TRUE);
  p->mCompressing = true;
}

void
JpegEncoder::onImageEnd()
{
  ::jpeg_finish_compress(&p->mCompressStruct);
  p->mCompressing = false;
}

void
JpegEncoder::onWriteLine(const void* data)
{
  auto linedata = JSAMPROW(data);
  int written = ::jpeg_write_scanlines(&p->mCompressStruct, &linedata, 1);
  if (written != 1)
    throw std::runtime_error("JpegEncoder: could not write scan line");
}
