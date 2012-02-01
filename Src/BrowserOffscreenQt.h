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

#ifndef BROWSEROFFSCREENQT_H
#define BROWSEROFFSCREENQT_H

#include "IpcBuffer.h"
#include "BrowserRect.h"
#include "BrowserOffscreenInfo.h"

class BrowserOffscreenCalculations;

class BrowserOffscreenQt
{
public:

    static BrowserOffscreenQt* create();
    static BrowserOffscreenQt* attach(int key, int size);
    ~BrowserOffscreenQt();

    inline IpcBuffer* ipcBuffer() const { return m_ipcBuffer; }
    inline int key() const { return m_ipcBuffer->key(); }
    inline int size() const { return m_ipcBuffer->size(); }
    inline BrowserOffscreenInfo* header() const { return m_header; }

    bool matchesParams(BrowserOffscreenCalculations* calc) const;
    void updateParams(BrowserOffscreenCalculations* calc);
    bool matchesParams(BrowserOffscreenQt* other) const;

    QImage* surface();
    void clear();
    void copyFrom(BrowserOffscreenQt* other, BrowserRect* rect=NULL);

    unsigned char* rasterBuffer() const { return m_buffer; }
    int rasterSize() const;

private:

    BrowserOffscreenQt(IpcBuffer* buffer);
    void resetBuffer();

    IpcBuffer* m_ipcBuffer;
    unsigned char* m_buffer;
    BrowserOffscreenInfo* m_header;
    QImage* m_surface;

    int m_contentWidth;
    int m_contentHeight;
    int m_viewportWidth;
    int m_viewportHeight;
};


#endif /* BROWSEROFFSCREENQT_H */
