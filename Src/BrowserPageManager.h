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

#ifndef BROWSERPAGEMANAGER_H
#define BROWSERPAGEMANAGER_H

#include <glib.h>

#include <list>
#include <string>

#include "BrowserPage.h"  // for identifier constant

class BrowserPage;

typedef struct WatchListEntry
{
    BrowserPage* page;
    int32_t      identifier;
    uint32_t     timeCreated;  // gettimeofday's tv_sec
} WatchListEntry_t;

class BrowserPageManager
{
public:
    
    static const uint32_t kPageWaitMaxSeconds = 10;  // timeout for BA to attach to BrowserPage
    
    static BrowserPageManager* instance();
    
    static gboolean            expireWatchedPages(gpointer);
    
    void registerPage(BrowserPage* page);
    void unregisterPage(BrowserPage* page);
    
    int  purgeLowPriorityPages();
    int numPages() const { return m_pageList.size(); }
    void raisePagePriority(BrowserPage* page);

    void setFocusedPage(BrowserPage* page, bool focused);
    BrowserPage* focusedPage() const { return m_focusedPage; }

    BrowserPage*  findInWatchedList(const int32_t identifier);
    void          watchForPage(BrowserPage* watchingPage, const int32_t identifier);
    int           removeFromWatchedList(const int32_t identifier);

private:

    BrowserPageManager();
    ~BrowserPageManager();

    BrowserPageManager(const BrowserPageManager&);
    BrowserPageManager& operator=(const BrowserPageManager&);
    
    static BrowserPageManager* m_instance;
    std::list<BrowserPage*> m_pageList;
    std::list<WatchListEntry_t> m_watchingPageList;
    static bool compareByPriority(BrowserPage* b1, BrowserPage* b2);
    BrowserPage* m_focusedPage;
};

#endif /* BROWSERPAGEMANAGER_H */
