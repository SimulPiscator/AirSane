/*
AirSane Imaging Daemon
Copyright (C) 2018-2023 Simul Piscator

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

#include "imageencoder.h"
#include <stdexcept>

ImageEncoder::ImageEncoder()
  : mWidth(0)
  , mHeight(0)
  , mComponents(0)
  , mBitDepth(0)
  , mDpi(0)
  , mOrientationDegrees(0)
  , mBytesPerLine(0)
  , mCurrentLine(0)
  , mCurrentImage(0)
  , mColorspace(Unknown)
  , mpDestination(nullptr)
{}

ImageEncoder&
ImageEncoder::setWidth(int w)
{
  mWidth = w;
  onParamChange();
  return *this;
}

int
ImageEncoder::width() const
{
  return mWidth;
}

ImageEncoder&
ImageEncoder::setHeight(int h)
{
  mHeight = h;
  onParamChange();
  return *this;
}

int
ImageEncoder::height() const
{
  return mHeight;
}

ImageEncoder&
ImageEncoder::setBitDepth(int b)
{
  mBitDepth = b;
  onParamChange();
  return *this;
}

int
ImageEncoder::bitDepth() const
{
  return mBitDepth;
}

ImageEncoder&
ImageEncoder::setResolutionDpi(int dpi)
{
  mDpi = dpi;
  onParamChange();
  return *this;
}

int
ImageEncoder::resolutionDpi() const
{
  return mDpi;
}

ImageEncoder&
ImageEncoder::setOrientationDegrees(int d)
{
  mOrientationDegrees = d;
  return *this;
}

int
ImageEncoder::orientationDegrees() const
{
  return mOrientationDegrees;
}

ImageEncoder&
ImageEncoder::setColorspace(ImageEncoder::Colorspace cs)
{
  mColorspace = cs;
  onParamChange();
  return *this;
}

ImageEncoder::Colorspace
ImageEncoder::colorspace() const
{
  return mColorspace;
}

int
ImageEncoder::components() const
{
  return mComponents;
}

ImageEncoder&
ImageEncoder::setDestination(std::ostream* p)
{
  mpDestination = p;
  onParamChange();
  return *this;
}

std::ostream*
ImageEncoder::destination() const
{
  return mpDestination;
}

ImageEncoder&
ImageEncoder::writeLine(const void* p)
{
  if (mCurrentLine == 0 && mCurrentImage == 0)
      onDocumentBegin();
  if (mCurrentLine == 0 && mpDestination)
    onImageBegin();
  if (mpDestination)
    onWriteLine(p);
  if (++mCurrentLine == mHeight) {
    mCurrentLine = 0;
    ++mCurrentImage;
  }
  if (mCurrentLine == 0 && mpDestination)
    onImageEnd();
  return *this;
}

ImageEncoder&
ImageEncoder::endDocument()
{
    if (mpDestination)
        onDocumentEnd();
    return *this;
}

int
ImageEncoder::bytesPerLine()
{
  return mBytesPerLine;
}

int
ImageEncoder::currentImage() const
{
  return mCurrentImage;
}

int
ImageEncoder::linesLeftInCurrentImage() const
{
  return mHeight - mCurrentLine;
}

int
ImageEncoder::encodedSize() const
{
  return onEncodedSize();
}

void
ImageEncoder::onParamChange()
{
  if (mCurrentLine != 0)
    throw std::runtime_error("cannot change settings inside an image");
  switch (mColorspace) {
    case Grayscale:
      mComponents = 1;
      break;
    case RGB:
      mComponents = 3;
      break;
    default:
      mComponents = 1;
  }
  mBytesPerLine = (mComponents * mWidth * mBitDepth) / 8;
}
