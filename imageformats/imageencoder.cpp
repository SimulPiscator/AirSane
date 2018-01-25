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

#include "imageencoder.h"
#include <stdexcept>

ImageEncoder &ImageEncoder::writeLine(const void * p)
{
    if(mCurrentLine == 0 && mpDestination)
        onImageBegin();
    if(mpDestination)
        onWriteLine(p);
    if(++mCurrentLine == mHeight)
        mCurrentLine = 0;
    if(mCurrentLine == 0 && mpDestination)
        onImageEnd();
    return *this;
}

void ImageEncoder::onParamChange()
{
    if(mCurrentLine != 0)
        throw std::runtime_error("cannot change settings inside an image");
    switch(mColorspace) {
    case Grayscale:
        mComponents = 1;
        break;
    case RGB:
        mComponents = 3;
        break;
    default:
        mComponents = 1;
    }
    mBytesPerLine = (mComponents*mWidth*mBitDepth)/8;
}
