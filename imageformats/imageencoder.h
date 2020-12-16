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

#ifndef IMAGEENCODER_H
#define IMAGEENCODER_H

#include <iostream>

class ImageEncoder
{
    ImageEncoder(const ImageEncoder&) = delete;
    ImageEncoder& operator=(const ImageEncoder&) = delete;

public:
    ImageEncoder();
    virtual ~ImageEncoder() {}

    ImageEncoder& setWidth(int w);
    int width() const;

    ImageEncoder& setHeight(int h);
    int height() const;

    ImageEncoder& setBitDepth(int b);
    int bitDepth() const;

    ImageEncoder& setResolutionDpi(int dpi);
    int resolutionDpi() const;

    enum Colorspace { Unknown, Grayscale, RGB };
    ImageEncoder& setColorspace(Colorspace cs);
    Colorspace colorspace() const;

    int components() const;

    ImageEncoder& setDestination(std::ostream* p);
    std::ostream* destination() const;

    ImageEncoder& writeLine(const void*);
    int bytesPerLine();
    int linesLeftInCurrentImage() const;

    int encodedSize() const;

protected:
    virtual int onEncodedSize() const { return -1; }
    virtual void onImageBegin() = 0;
    virtual void onImageEnd() = 0;
    virtual void onWriteLine(const void*) = 0;

    void onParamChange();

private:
    int mWidth, mHeight, mComponents, mBitDepth, mDpi;
    int mBytesPerLine, mCurrentLine;
    Colorspace mColorspace;
    std::ostream* mpDestination;
};


#endif // IMAGEENCODER_H
