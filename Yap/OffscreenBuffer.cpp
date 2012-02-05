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

#include <glib.h>
#include <stdio.h>
#include <math.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

extern "C" {
#include <png.h>
}

#include "ProcessMutex.h"
#include "OffscreenBuffer.h"


class OffscreenRect
{
public:

    int left;
    int right;
    int top;
    int bottom;

    OffscreenRect() {
        left = 0;
        top = 0;
        right = 0;
        bottom = 0;
    }

    OffscreenRect(int l, int t, int r, int b) {
        left = l;
        top = t;
        right = r;
        bottom = b;
    }

    bool empty() const {
        return (right <= left || bottom <= top);
    }

    bool intersects(const OffscreenRect& other) const {

        if (empty() || other.empty())
            return false;

        int l = MAX(left, other.left);
        int r = MIN(right, other.right);
        int t = MAX(top, other.top);
        int b = MIN(bottom, other.bottom);

        return (l < r) && (t < b);
    }

    void intersect(const OffscreenRect& other) {

        int l = MAX(left, other.left);
        int r = MIN(right, other.right);
        int t = MAX(top, other.top);
        int b = MIN(bottom, other.bottom);

        if (r <= l || b <= t) {
            left = 0;
            top = 0;
            right = 0;
            bottom = 0;
        }
        else {
            left = l;
            top = t;
            right = r;
            bottom = b;
        }
    }
};

class OffscreenMutexLocker
{
public:

    OffscreenMutexLocker(ProcessMutex* mutex) {
        m_mutex = mutex;
        m_mutex->lock();
    }

    ~OffscreenMutexLocker() {
        m_mutex->unlock();
    }

    ProcessMutex* m_mutex;
};


OffscreenBuffer::OffscreenBuffer(int width, int height)
    : m_mutex(0)
    , m_buffer(0)
    , m_bufferSize(width * height * 4)
{
    m_mutex = new ProcessMutex(sizeof(BufferInfo));
    if (!m_mutex || !m_mutex->isValid())
        return;

    BufferInfo* info = (BufferInfo*) m_mutex->data();
    info->bufferWidth = width;
    info->bufferHeight = height;

    info->width = width;
    info->height = height;
    info->stride = width;
    info->xPadding = 0;
    info->yPadding = 0;

    info->viewportWidth = 0;
    info->viewportHeight = 0;
    info->contentsWidth = 0;
    info->contentsHeight = 0;

    info->stride = width;
    info->scrollX = 0;
    info->scrollY = 0;

    // allocate rendering buffer
    int key = -1;
    while (key < 0) {

        struct timeval tv;
        gettimeofday(&tv, NULL);
        key_t k = ftok(".", tv.tv_usec);
        key = ::shmget(k, width * height * 4, 0644 | IPC_CREAT | IPC_EXCL);
        if (key == -1 && errno != EEXIST) {
            fprintf(stderr, "OffscreenBuffer: failed to create rendering buffer %s\n", strerror(errno));
            return;
        }
    }

    info->bufferId = key;

    m_buffer = (uint32_t*) ::shmat(info->bufferId, NULL, 0);
    if (-1 == (int)m_buffer) {
        fprintf(stderr, "ERROR %d attaching to shared memory key %d: %s\n", errno, info->bufferId, strerror(errno));
        m_buffer = 0;
        return;
    }

    // Auto delete when all processes detach
    ::shmctl(info->bufferId, IPC_RMID, NULL);

    if( m_buffer ) 
        ::memset( m_buffer, 0xff, m_bufferSize);
}

OffscreenBuffer::OffscreenBuffer(int key)
    : m_mutex(0)
    , m_buffer(0)
    , m_bufferSize(0)
{
    m_mutex = new ProcessMutex(sizeof(BufferInfo), key);
    if (!m_mutex || !m_mutex->isValid())
        return;

    OffscreenMutexLocker locker(m_mutex);

    BufferInfo* info = (BufferInfo*) m_mutex->data();
    m_buffer = (uint32_t*) ::shmat(info->bufferId, NULL, 0);
    if (-1 == (int)m_buffer) {
        fprintf(stderr, "ERROR %d attaching to shared memory key %d: %s\n", errno, info->bufferId, strerror(errno));
        m_buffer = 0;
        return;
    }
    m_bufferSize = info->bufferWidth * info->bufferHeight * 4;
    if( m_buffer ) 
        ::memset( m_buffer, 0xff, m_bufferSize);
}

OffscreenBuffer::~OffscreenBuffer()
{
    if (m_buffer)
        ::shmdt(m_buffer);

    delete m_mutex;
}

void OffscreenBuffer::getDimensions(int& width, int& height)
{
    OffscreenMutexLocker locker(m_mutex);

    BufferInfo* info = (BufferInfo*) m_mutex->data();
    width = info->width;
    height = info->height;
}

/**
 * Return the offscreen buffer's content rectangle.
 *
 * @param cx The content rectangle X scroll position.
 * @param cy The content rectangle Y scroll position.
 * @param cw The content rectangle width.
 * @param ch The content rectangle height.
 */
void OffscreenBuffer::getContentRect(int& cx, int& cy, int& cw, int& ch)
{
    OffscreenMutexLocker locker(m_mutex);

    BufferInfo* info = (BufferInfo*) m_mutex->data();
    cx = info->scrollX;
    cy = info->scrollY;
    cw = info->width;
    ch = info->height;
}

void OffscreenBuffer::viewportSizeChanged(int w, int h)
{
    OffscreenMutexLocker locker(m_mutex);

    BufferInfo* info = (BufferInfo*) m_mutex->data();
    info->viewportWidth = w;
    info->viewportHeight = h;

    if (info->viewportWidth  > info->width ||
        info->viewportHeight > info->height) {
        printf("OffscreenBuffer: internal buffer size is smaller than the viewport size: "
               " viewport (%d, %d) buffer (%d, %d)\n",
               info->viewportWidth, info->viewportHeight,
               info->width, info->height);
    }

    info->xPadding = abs(info->width - info->viewportWidth) / 2;
    info->yPadding = abs(info->height - info->viewportHeight) / 2;
}

void OffscreenBuffer::contentsSizeChanged(int w, int h)
{
    OffscreenMutexLocker locker(m_mutex);

    BufferInfo* info = (BufferInfo*) m_mutex->data();

    // If the width doesn't match or the page dimensions changed to 0x0 (which means a url
    // change, we will just invalidate the whole content
    if (info->contentsWidth != w || (w == 0 && h == 0)) {
        invalidate();
        info->scrollX = - info->bufferWidth;
        info->scrollY = - info->bufferHeight;
    }

    if (w == 0) {
        info->width = 0;
        info->height = 0;
        info->stride = 0;
        info->xPadding = 0;
        info->yPadding = 0;
    }
    else if ( info->contentsWidth != w) {

        // page width changed, we will determine our buffer size
        int maxWidth = (int) sqrt(info->bufferWidth * info->bufferHeight);

        info->width = MIN(w, maxWidth);
        info->height = (info->bufferWidth * info->bufferHeight) / info->width;
        info->height = MIN(h, info->height);

        info->stride = info->width;
        info->xPadding = abs(info->width - info->viewportWidth) / 2;
        info->yPadding = abs(info->height - info->viewportHeight) / 2;

        printf("Contents changed: %d, %d    %d, %d, %d, %d\n",
               w, h, info->width, info->height, info->xPadding, info->yPadding);
    }
    else {

        // width is the same. height has changed.

        info->height = (info->bufferWidth * info->bufferHeight) / info->width;
        info->height = MIN(h, info->height);
        info->yPadding = (info->height - info->viewportHeight) / 2;

        printf("Contents changed: %d, %d    %d, %d, %d, %d\n",
               w, h,
               info->width, info->height, info->xPadding, info->yPadding);
    }

    info->contentsWidth = w;
    info->contentsHeight = h;
}

bool OffscreenBuffer::scrollChanged(int& x, int& y)
{
    bool ret = false;

    OffscreenMutexLocker locker(m_mutex);

    BufferInfo* info = (BufferInfo*) m_mutex->data();

    if (x + info->viewportWidth > info->contentsWidth)
        x = info->contentsWidth - info->viewportWidth;

    if (y + info->viewportHeight > info->contentsHeight)
        y = info->contentsHeight - info->viewportHeight;

    if (x < 0)
        x = 0;

    if (y < 0)
        y = 0;

    // We change the scroll offset if any of the following 4 happens:
    // * right edge of viewport window > right edge of buffer window
    // * bottom edge of viewport window > bottom edge of buffer window
    // * left edge of viewport window < left edge of buffer window
    // * top edge of viewport window < left edge of buffer window

    if ((info->scrollX > x) ||
        (info->scrollX + info->width) < MIN(x + info->viewportWidth, info->contentsWidth)) {

        x -= info->xPadding;
        x = MAX(x, 0);

        ret = true;
    }

    if ((info->scrollY > y) ||
        (info->scrollY + info->height) < MIN(y + info->viewportHeight, info->contentsHeight)) {

        y -= info->yPadding;
        y = MAX(y, 0);

        ret = true;
    }


    return ret;
}

bool OffscreenBuffer::scrollAndContentsChanged(int x, int y, int w, int h)
{
    bool ret = true;

    contentsSizeChanged(w, h);
    scrollChanged(x, y);

    return ret;
}

int OffscreenBuffer::key() const
{
    return m_mutex->key();
}

void OffscreenBuffer::invalidate()
{
    ::memset(m_buffer, 0xFF, m_bufferSize);
}

/**
 * Erase this offscreen buffer. Set's every pixel to white (opaque).
 */
void OffscreenBuffer::erase()
{
    OffscreenMutexLocker locker(m_mutex);

    invalidate();
}

void OffscreenBuffer::copyFromBuffer(uint32_t* srcBuffer, int srcStride, int srcPositionX, int srcPositionY, int srcSizeWidth, int srcSizeHeight)
{
    OffscreenMutexLocker locker(m_mutex);

    BufferInfo* info = (BufferInfo*) m_mutex->data();

    OffscreenRect srcRect(srcPositionX, srcPositionY, srcPositionX + srcSizeWidth, srcPositionY + srcSizeHeight);
    OffscreenRect dstRect(info->scrollX, info->scrollY, info->scrollX + info->width, info->scrollY + info->height);
    srcRect.intersect(dstRect);

    uint32_t* src = 0;
    uint32_t* dst = 0;
    uint32_t* sp = 0;
    uint32_t* dp = 0;

    if (srcRect.empty())
        return;

    src = srcBuffer + (srcRect.top - srcPositionY) * srcStride + (srcRect.left - srcPositionX);
    dst = m_buffer + (srcRect.top - info->scrollY) * info->stride + (srcRect.left - info->scrollX);

    for (int j = (srcRect.bottom - srcRect.top); j > 0; j--) {
        sp = src;
        dp = dst;
        src += srcStride;
        dst += info->stride;

        for (int i = (srcRect.right - srcRect.left); i > 0; i--) {
            *dp++ = *sp++;

            __builtin_prefetch(sp + 16);
        }
    }
}

void OffscreenBuffer::copyFromBuffer(uint32_t* srcBuffer, int srcStride, int srcPositionX, int srcPositionY, int srcSizeWidth, int srcSizeHeight, int newScrollX, int newScrollY)
{
    OffscreenMutexLocker locker(m_mutex);

    BufferInfo* info = (BufferInfo*) m_mutex->data();

    // updating the scroll position
    //printf("Setting scroll position: %d, %d\n", newScrollX, newScrollY);

    info->scrollX = newScrollX;
    info->scrollY = newScrollY;

    invalidate();

    // updating the buffer

    OffscreenRect srcRect(srcPositionX, srcPositionY, srcPositionX + srcSizeWidth, srcPositionY + srcSizeHeight);
    OffscreenRect dstRect(info->scrollX, info->scrollY, info->scrollX + info->width, info->scrollY + info->height);

    //printf("Original srcRect: %d:%d, %d:%d\n", srcRect.left, srcRect.right, srcRect.top, srcRect.bottom);

    srcRect.intersect(dstRect);

    // printf("Paint: %d:%d, %d:%d\n", srcRect.left, srcRect.right, srcRect.top, srcRect.bottom);

    // printf("Final srcRect: %d:%d, %d:%d\n", srcRect.left, srcRect.right, srcRect.top, srcRect.bottom);

    uint32_t* src = 0;
    uint32_t* dst = 0;
    uint32_t* sp = 0;
    uint32_t* dp = 0;

    if (srcRect.empty())
        return;

    src = srcBuffer + (srcRect.top - srcPositionY) * srcStride + (srcRect.left - srcPositionX);
    dst = m_buffer + (srcRect.top - info->scrollY) * info->stride + (srcRect.left - info->scrollX);

    for (int j = (srcRect.bottom - srcRect.top); j > 0; j--) {
        sp = src;
        dp = dst;
        src += srcStride;
        dst += info->stride;

        for (int i = (srcRect.right - srcRect.left); i > 0; i--) {
            *dp++ = *sp++;

            __builtin_prefetch(sp + 16);
        }
    }
}

void OffscreenBuffer::copyToBuffer(uint32_t* dstBuffer, int dstStride, int dstPositionX, int dstPositionY, int dstSizeWidth, int dstSizeHeight)
{
    OffscreenMutexLocker locker(m_mutex);

    BufferInfo* info = (BufferInfo*) m_mutex->data();

    OffscreenRect srcRect(info->scrollX, info->scrollY, info->scrollX + info->width, info->scrollY + info->height);
    OffscreenRect dstRect(dstPositionX, dstPositionY, dstPositionX + dstSizeWidth, dstPositionY + dstSizeHeight);

    srcRect.intersect(dstRect);

    uint32_t* src = 0;
    uint32_t* dst = 0;
    uint32_t* sp = 0;
    uint32_t* dp = 0;

    if (srcRect.empty())
        return;

    //printf("src Rect: %d:%d, %d:%d\n", srcRect.left, srcRect.right, srcRect.top, srcRect.bottom);

    src = m_buffer + (srcRect.top - info->scrollY) * info->stride + (srcRect.left - info->scrollX);
    dst = dstBuffer + (srcRect.top - dstRect.top) * dstStride + (srcRect.left - dstRect.left);

    for (int j = (srcRect.bottom - srcRect.top); j > 0; j--) {
        sp = src;
        dp = dst;
        src += info->stride;
        dst += dstStride;
        for (int i = (srcRect.right - srcRect.left); i > 0; i--) {
            *dp++ = *sp++;

            __builtin_prefetch(sp + 16);
        }
    }
}

static void PrvScale(uint32_t* src, int srcWidth, int srcHeight, int srcStride, uint32_t* dst, int dstWidth, int dstHeight, int dstStride)
{
    uint32_t  xinc, yinc;
    uint32_t  *sp, *sptr;
    uint32_t  *dp, *dptr;
    uint32_t  xacc, yacc;
    uint32_t  iindex, jindex;
    int32_t   i, j;

    if (srcWidth <= 1 || srcHeight <= 1 || dstWidth <= 1 || dstHeight <= 1)
        return;

    xinc = ((srcWidth  - 1) << 16) / (dstWidth  - 1);
    yinc = ((srcHeight - 1) << 16) / (dstHeight - 1);

    sp = src;
    dp = dst;

    yacc = 0;
    for (j = 0; j < dstHeight; j++)
    {
        jindex = yacc >> 16;

        sptr = sp + jindex * srcStride;
        dptr = dp + j * dstStride;

        xacc = 0;

        for (i = dstWidth; i > 0; i--)
        {
            iindex = xacc >> 16;
            *dptr++ = *(sptr + iindex);
            xacc += xinc;
        }

        yacc  += yinc;
    }
}

void OffscreenBuffer::scaleToBuffer(uint32_t* dstBuffer, int dstStride, int dstLeft, int dstTop, int dstRight, int dstBottom, int srcLeft, int srcTop, int srcRight, int srcBottom, double scale)
{
    OffscreenMutexLocker locker(m_mutex);

    BufferInfo* info = (BufferInfo*) m_mutex->data();

    printf("Src: %d:%d, %d:%d\n", srcLeft, srcRight, srcTop, srcBottom);
    printf("Dst: %d:%d, %d:%d\n", dstLeft, dstRight, dstTop, dstBottom);

    OffscreenRect srcRect(srcLeft, srcTop, srcRight, srcBottom);

    OffscreenRect bufferRect(info->scrollX, info->scrollY, info->scrollX + info->width, info->scrollY + info->height);

    printf("Buf: %d:%d, %d:%d\n", bufferRect.left, bufferRect.right, bufferRect.top, bufferRect.bottom);

    srcRect.intersect(bufferRect);
    if (srcRect.empty())
        return;

    printf("Int: %d:%d, %d:%d\n", srcRect.left, srcRect.right, srcRect.top, srcRect.bottom);

    uint32_t* src = 0;
    uint32_t* dst = 0;

    OffscreenRect dstScaledRect((int) (srcRect.left * scale), (int) (srcRect.top * scale), (int) (srcRect.right * scale), (int) (srcRect.bottom * scale));

    dstScaledRect.intersect(OffscreenRect(dstLeft, dstTop, dstRight, dstBottom));

    printf("Mod: %d:%d, %d:%d\n", dstScaledRect.left, dstScaledRect.right, dstScaledRect.top, dstScaledRect.bottom);


    src = m_buffer + (srcRect.top - info->scrollY) * info->stride + (srcRect.left - info->scrollX);
    dst = dstBuffer + (dstScaledRect.top - dstTop) * dstStride + (dstScaledRect.left - dstLeft);

    PrvScale(src, srcRect.right - srcRect.left, srcRect.bottom - srcRect.top, info->stride, dst, dstScaledRect.right - dstScaledRect.left, dstScaledRect.bottom - dstScaledRect.top, dstStride);
}

void OffscreenBuffer::dump(const char* fileName)
{
    uint32_t*       ptr;
    uint8_t         pixel[3];
    int             numErrors(0);

    OffscreenMutexLocker locker(m_mutex);

    BufferInfo* info = (BufferInfo*) m_mutex->data();
    int width = info->width;
    int height = info->height;

    FILE* pFile = fopen(fileName, "wb");
    if (pFile) {
        fprintf(pFile, "P6\n" "%d %d\n" "255\n", width, height);

        for (int j = 0; j < height; j++) {
            ptr = (uint32_t*) m_buffer + j * width;
            for (int i = 0; i < width; i++) {
                pixel[2] = (*ptr >>  0)  & 0xFF;
                pixel[1] = (*ptr >>  8)  & 0xFF;
                pixel[0] = (*ptr >> 16)  & 0xFF;
                if (3 != fwrite(pixel, 1, 3, pFile)) {
                    numErrors++;
                }
                ptr++;
            }
        }
        fclose(pFile);
    }
    if (numErrors != 0) {
        fprintf(stderr, "Errors dumping file\n");
    }
}

/**
 * Write the offscreen browser image buffer to a 32-bit lossless PNG file.
 *
 * @note All coordinates are view relative (i.e. relative to the current scroll position ) and
 * <strong>not</strong> relative to the origin of the page.
 *
 * @param pFile The open file pointer to put the offscreen buffer into.
 * @param viewLeft The left edge of the rectangle to save (view relative).
 * @param viewTop The top edge of the rectangle to save (view relative).
 * @param viewRight The right edge of the rectangle to save (view relative).
 * @param viewBottom The bottom edge of the rectangle to save (view relative).
 *
 * @return The error code. Zero means success.
 */
int OffscreenBuffer::writeToFile(FILE* pFile, int viewLeft, int viewTop, int viewRight, int viewBottom) const
{
    OffscreenMutexLocker locker(m_mutex);

    BufferInfo* info = (BufferInfo*) m_mutex->data();

    int nErr = 0;
    const png_byte byBitDepth(8);
    uint32_t* scanline(NULL);

    assert( viewTop < viewBottom );
    assert( viewLeft < viewRight );

    png_structp pPngImage = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (NULL == pPngImage) {
        nErr = ENOMEM; // Don't know why this will fail, let's say insufficient memory.
    }

    int nImgWidth = viewRight - viewLeft;
    int nImgHeight = viewBottom - viewTop;
    scanline = new uint32_t[nImgWidth];
    if (NULL == scanline) {
        nErr = ENOMEM;
    }
    ::memset(scanline, 0, nImgWidth*sizeof(uint32_t)); // Just to avoid valgrind warning

    png_infop pPngInfo(NULL);
    if (!nErr) {
        pPngInfo = png_create_info_struct(pPngImage);
        if (NULL == pPngInfo) {
            nErr = ENOMEM; // Don't know why this will fail, let's say insufficient memory.
        }
    }

    // Initialize I/O
    if ( !nErr ) {
        if ( setjmp( png_jmpbuf(pPngImage) ) )
            nErr = EIO;
        else
            png_init_io(pPngImage, pFile);
    }

    // Write the header
    if ( !nErr ) {
        if (setjmp(png_jmpbuf(pPngImage))) {
            nErr = EIO;
        }
        else {
            png_set_IHDR(pPngImage, pPngInfo, nImgWidth, nImgHeight, byBitDepth, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

            png_write_info(pPngImage, pPngInfo);
        }
    }

    OffscreenRect srcRect(info->scrollX, info->scrollY, info->scrollX + info->width, info->scrollY + info->height);
    OffscreenRect dstRect(viewLeft, viewTop, viewRight, viewBottom);
    srcRect.intersect(dstRect);

    // Write the image data
    if (!srcRect.empty() && !nErr) {

        uint32_t* src = m_buffer + (srcRect.top - info->scrollY) * info->stride + (srcRect.left - info->scrollX);

        for (int j = (srcRect.bottom - srcRect.top); !nErr && j > 0; j--) {

            uint32_t* sp = src;
            unsigned char* r = reinterpret_cast<unsigned char*>(scanline);

            src += info->stride;

            for (int i = (srcRect.right - srcRect.left); !nErr && i > 0; i--) {
                *r++ = (*sp >> 16) & 0xFF;
                *r++ = (*sp >>  8) & 0xFF;
                *r++ = (*sp >>  0) & 0xFF;
                *r++ = 0xFF;
                sp++;

                assert( (r - reinterpret_cast<unsigned char*>(scanline)) <= (nImgWidth*4) );
            }

            if (setjmp(png_jmpbuf(pPngImage))) {
                nErr = EIO;
            }
            else {
                png_write_row( pPngImage, reinterpret_cast<unsigned char*>(scanline) );
            }
        }
    }

    delete [] scanline;

    // Close file
    if ( NULL != pPngImage ) {
        if (setjmp(png_jmpbuf(pPngImage))) {
            nErr = EIO;
        }
        else {
            png_write_end(pPngImage, NULL);
        }
    }

    return nErr;
}
