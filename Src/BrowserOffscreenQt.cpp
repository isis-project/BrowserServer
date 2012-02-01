/* @@@LICENSE
*
*      Copyright (c) 2012 Hewlett-Packard Development Company, L.P.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
LICENSE@@@ */

#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <qimage.h>

#include "BrowserOffscreenQt.h"
#include "BrowserOffscreenCalculations.h"
#include "BrowserRect.h"

static const float kOffscreenSizeAsScreenSizeMultiplier = 4.0f;

// Used only for desktop builds
static const int kDefaultScreenWidth = 1024;
static const int kDefaultScreenHeight = 768;

static double kDoubleZeroTolerance = 0.0001;

static bool PrvGetScreenDimensions(int& width, int& height)
{
#if defined(TARGET_DESKTOP)
    width = 1024;
    height = 768;
    return true;
#endif

    int fd = ::open("/dev/fb0", O_RDONLY);
    if (fd < 0)
        return false;

    struct fb_var_screeninfo varinfo;
    ::memset(&varinfo, 0, sizeof(varinfo));

    if (::ioctl(fd, FBIOGET_VSCREENINFO, &varinfo) == -1) {
        ::close(fd);
        return false;
    }

    width = varinfo.xres;
    height = varinfo.yres;
    ::close(fd);

    return true;
}

static inline bool PrvIsEqual(double a, double b)
{
    return (fabs(a-b) < kDoubleZeroTolerance);
}

BrowserOffscreenQt* BrowserOffscreenQt::create()
{
    int screenWidth, screenHeight;
    if (!PrvGetScreenDimensions(screenWidth, screenHeight)) {
        screenWidth = kDefaultScreenWidth;
        screenHeight = kDefaultScreenHeight;
    }

    int bufferSize = screenWidth *
                     screenHeight *
                     sizeof(unsigned int) *
                     kOffscreenSizeAsScreenSizeMultiplier;

    IpcBuffer* buffer = IpcBuffer::create(bufferSize + sizeof(BrowserOffscreenInfo));
    if (!buffer) {
        return 0;
    }
    return new BrowserOffscreenQt(buffer);
}

BrowserOffscreenQt* BrowserOffscreenQt::attach(int key, int size)
{
printf("\nBrowserOffscreenQt::attach(int key = %d, int size%d)\n\n", key, size);
    IpcBuffer* buffer = IpcBuffer::attach(key, size);
    if (!buffer)
        return 0;

    return new BrowserOffscreenQt(buffer);
}

BrowserOffscreenQt::BrowserOffscreenQt(IpcBuffer* ipcBuffer)
    : m_ipcBuffer(ipcBuffer)
    , m_buffer((unsigned char*)ipcBuffer->buffer() + sizeof(BrowserOffscreenInfo))
    , m_header((BrowserOffscreenInfo*)m_ipcBuffer->buffer())
    , m_surface(0)
{
    resetBuffer();
}

BrowserOffscreenQt::~BrowserOffscreenQt()
{
    delete m_surface;
    delete m_ipcBuffer;
}

void BrowserOffscreenQt::clear()
{
    if (m_buffer)
        ::memset(m_buffer, 0xFF, rasterSize());
}

void BrowserOffscreenQt::copyFrom(BrowserOffscreenQt* other,  BrowserRect* r)
{
    // Check if we can actually copy from this buffer
    if (m_header->bufferWidth  != other->m_header->bufferWidth ||
        m_header->bufferHeight != other->m_header->bufferHeight ||
        !PrvIsEqual(m_header->contentZoom, other->m_header->contentZoom))
        return;

    BrowserRect myRect(m_header->renderedX,
                       m_header->renderedY,
                       m_header->renderedWidth,
                       m_header->renderedHeight);

    BrowserRect otherRect(other->m_header->renderedX,
                          other->m_header->renderedY,
                          other->m_header->renderedWidth,
                          other->m_header->renderedHeight);

    if (!myRect.intersects(otherRect))
        return;

    myRect.intersect(otherRect);
    if (r) {
        if (!myRect.intersects(*r)) return;
        myRect.intersect(*r);
    }

    unsigned int* src = (unsigned int*) other->rasterBuffer();
    unsigned int* dst = (unsigned int*) rasterBuffer();
    int stride        = m_header->renderedWidth;

    src += (myRect.y() - other->m_header->renderedY) * stride +
           (myRect.x() - other->m_header->renderedX);
    dst += (myRect.y() - m_header->renderedY) * stride +
           (myRect.x() - m_header->renderedX);

    for (int j = 0; j < myRect.h(); j++) {
        ::memcpy(dst, src, myRect.w() * sizeof(unsigned int));
        src += stride;
        dst += stride;
    }
}

QImage* BrowserOffscreenQt::surface()
{
    if (m_surface) {
        if (((int)m_surface->width() == m_header->renderedWidth) &&
            ((int)m_surface->height() == m_header->renderedHeight)) {
            return m_surface;
        }
        else {
            delete m_surface;
            m_surface = 0;
        }
    }

    if ((m_header->renderedWidth > 0) && (m_header->renderedHeight > 0)) {
        m_surface = new QImage(m_buffer,
                               m_header->renderedWidth,
                               m_header->renderedHeight,
                               QImage::Format_ARGB32_Premultiplied);
    }

    return m_surface;
}

void BrowserOffscreenQt::resetBuffer()
{
    if (m_surface) {
        delete m_surface;
        m_surface = 0;
    }

    if (m_header)
        ::memset(m_header, 0, sizeof(BrowserOffscreenInfo));
}

bool BrowserOffscreenQt::matchesParams(BrowserOffscreenCalculations* calc) const
{
    if (!m_header)
        return false;

    return
        m_header->bufferWidth  == calc->bufferWidth &&
        m_header->bufferHeight == calc->bufferHeight &&
        m_header->renderedX == calc->renderX &&
        m_header->renderedY == calc->renderY &&
        m_header->renderedWidth == calc->renderWidth &&
        m_header->renderedHeight == calc->renderHeight &&
        PrvIsEqual(m_header->contentZoom, calc->contentZoom);
}

void BrowserOffscreenQt::updateParams(BrowserOffscreenCalculations* calc)
{
    if (!m_header)
        return;

    resetBuffer();

    m_header->bufferWidth  = calc->bufferWidth;
    m_header->bufferHeight = calc->bufferHeight;

    m_header->contentZoom = calc->contentZoom;

    m_header->renderedX = calc->renderX;
    m_header->renderedY = calc->renderY;
    m_header->renderedWidth = calc->renderWidth;
    m_header->renderedHeight = calc->renderHeight;
}

bool BrowserOffscreenQt::matchesParams(BrowserOffscreenQt* other) const
{
    if (!m_header)
        return false;

    return
        m_header->bufferWidth  == other->m_header->bufferWidth &&
        m_header->bufferHeight == other->m_header->bufferHeight &&
        m_header->renderedX == other->m_header->renderedX &&
        m_header->renderedY == other->m_header->renderedY &&
        m_header->renderedWidth == other->m_header->renderedWidth &&
        m_header->renderedHeight == other->m_header->renderedHeight &&
        PrvIsEqual(m_header->contentZoom, other->m_header->contentZoom);
}

int BrowserOffscreenQt::rasterSize() const
{
    return m_ipcBuffer->size() - sizeof(BrowserOffscreenInfo); 
}

