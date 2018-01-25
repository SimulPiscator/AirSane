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

#ifndef IMAGEENCODER_H
#define IMAGEENCODER_H

#include <iostream>

class ImageEncoder
{
public:
    ImageEncoder() : mWidth(0), mHeight(0), mComponents(0), mBitDepth(0), mDpi(0),
        mBytesPerLine(0), mColorspace(Unknown), mpDestination(nullptr), mCurrentLine(0)
    {}
    virtual ~ImageEncoder() {}

    ImageEncoder& setWidth(int w) { mWidth = w; onParamChange(); return *this; }
    int width() const { return mWidth; }
    ImageEncoder& setHeight(int h) { mHeight = h; onParamChange(); return *this; }
    int height() const { return mHeight; }
    ImageEncoder& setBitDepth(int b) { mBitDepth = b; onParamChange(); return *this; }
    int bitDepth() const { return mBitDepth; }
    ImageEncoder& setResolutionDpi(int dpi) { mDpi = dpi; onParamChange(); return *this; }
    int resolutionDpi() const { return mDpi; }
    enum Colorspace { Unknown, Grayscale, RGB };
    ImageEncoder& setColorspace(Colorspace cs) { mColorspace = cs; onParamChange(); return *this; }
    Colorspace colorspace() const { return mColorspace; }
    int components() const { return mComponents; }

    ImageEncoder& setDestination(std::ostream* p) { mpDestination = p; onParamChange(); return *this; }
    std::ostream* destination() const { return mpDestination; }

    ImageEncoder& writeLine(const void*);
    int bytesPerLine() { return mBytesPerLine; }
    int linesLeftInCurrentImage() const { return mHeight - mCurrentLine; }

    int encodedSize() const { return onEncodedSize(); }

protected:
    virtual int onEncodedSize() const { return -1; }
    virtual void onImageBegin() = 0;
    virtual void onImageEnd() = 0;
    virtual void onWriteLine(const void*) = 0;

    void onParamChange();

private:
    int mHeight, mWidth, mComponents, mBitDepth, mDpi, mBytesPerLine, mCurrentLine;
    Colorspace mColorspace;
    std::ostream* mpDestination;
};


#endif // IMAGEENCODER_H
