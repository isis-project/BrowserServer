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

#include <time.h>
#include <syslog.h>

#include "BrowserPageManager.h"
#include "BrowserPage.h"
#include "BrowserServer.h"

BrowserPageManager* BrowserPageManager::m_instance = 0;

BrowserPageManager* BrowserPageManager::instance()
{
    if (!m_instance)
        new BrowserPageManager();

    return m_instance;
}

BrowserPageManager::BrowserPageManager()
    : m_focusedPage(0)
{
    m_instance = this;
}

BrowserPageManager::~BrowserPageManager()
{
    m_instance = 0;
}

void
BrowserPageManager::registerPage(BrowserPage* page)
{
    m_pageList.push_back(page);
}

/**
 * @brief Checks if any pages in waitlist have been waiting for a BrowserAdapter
 *        for too long. Destroys BrowserPage 
 *
 */
gboolean
BrowserPageManager::expireWatchedPages(gpointer)
{

    BrowserPageManager* bpManager = BrowserPageManager::instance();

    if (bpManager->m_watchingPageList.empty()) {
        return FALSE;  // detaches this function from event loop
    }

    // current time in seconds
    struct timeval now;
    gettimeofday(&now, 0);

    std::list<WatchListEntry_t>::iterator iter_page = bpManager->m_watchingPageList.begin();
    while(iter_page != bpManager->m_watchingPageList.end())  
    {
        uint32_t pageWaitTime = now.tv_sec - (*iter_page).timeCreated;

        if (pageWaitTime > kPageWaitMaxSeconds) {
            (*iter_page).page->pageStop();
            BrowserPage *page = (*iter_page).page;
            // Remove the iterator from the list before deleting the page because the BrowserPage destructor
            // will call BrowserPageManager::unregisterPage that also calls m_watchingPageList.erase(iter_page);
            // which causes a double delete of the element in m_watchingPageList.
            iter_page = bpManager->m_watchingPageList.erase(iter_page);  // increments iterator
            delete page;
            page = 0;
        } else {
            ++iter_page;
        }
    }
    return TRUE;
}


void
BrowserPageManager::unregisterPage(BrowserPage* page)
{
    m_pageList.remove(page);

    std::list<WatchListEntry_t>::iterator it = m_watchingPageList.begin();
    for (; it != m_watchingPageList.end(); ++it) {
        if ((*it).page == page) {
            m_watchingPageList.erase(it);

            break;
        }
    }
}

void
BrowserPageManager::watchForPage(BrowserPage* watchingPage, const int32_t identifier)
{
    if (m_watchingPageList.empty()) {  // attach page expire function to main loop
        g_idle_add(expireWatchedPages, NULL);
    }

    struct timeval now;
    gettimeofday(&now, 0);

    WatchListEntry_t pageEntry;
    pageEntry.page = watchingPage;
    pageEntry.identifier = identifier;
    pageEntry.timeCreated = now.tv_sec;

    m_watchingPageList.push_back(pageEntry);
}

void BrowserPageManager::setFocusedPage(BrowserPage* page, bool focused)
{
    if (focused) {
        m_focusedPage = page;
        raisePagePriority(page);
    } else {
        if (m_focusedPage == page)
            m_focusedPage = 0;
    }
}

/**
 * @brief Applies priority policy to raise value of given page.
 *
 */
void
BrowserPageManager::raisePagePriority(BrowserPage* page)
{
    struct timespec currTime;
    ::clock_gettime(CLOCK_MONOTONIC, &currTime);

    page->setPriority((uint32_t)currTime.tv_sec);
}


/**
 * @brief Find BrowserPage instance by identifier in list of "watched" pages.
 *
 *
 */
BrowserPage* 
BrowserPageManager::findInWatchedList(const int32_t identifier)
{
    std::list<WatchListEntry_t>::const_iterator it = m_watchingPageList.begin();
    for (; it != m_watchingPageList.end(); ++it) {
        if ((*it).identifier == identifier) {
            return (*it).page;
        }
    }

    return NULL;
}


int
BrowserPageManager::removeFromWatchedList(const int32_t identifier)
{
    std::list<WatchListEntry_t>::iterator it = m_watchingPageList.begin();
    for (; it != m_watchingPageList.end(); ++it) {
        if ((*it).identifier == identifier) {
            m_watchingPageList.erase(it);
            break;
        }
    }

    return m_watchingPageList.size();
}

/**
 * @brief Applies purge policy to list of open pages. Sends events to app
 *        that page must be purged. App responds with asyncCmdDisconnect,
 *        which then deletes the page.
 */
int
BrowserPageManager::purgeLowPriorityPages()
{
    int numPurged = 0;
    // There are several different metrics here to determine
    // how many pages to try to purge:

    // 1) Purge half of the remaining pages.  (This creates too high a
    // load when trying to purge so many pages.)
    //int maxToPurge = (m_pageList.size() + 1) / 2;

    // 2) Purge one page at a time.  (This creates the smoothest load
    // but takes the longest to recover from critical memory.)
    int maxToPurge = 1;

    // 3) Purge an increasing number of pages on each iteration.
    //(This is smooth at the beginning but really loads up later on
    //when the system is already critical).
    //static purgePageCount = 1;
    //int maxToPurge = purgePageCount;
    //purgePageCount *= 2;

    // sort list of pages in ascending priority order
    m_pageList.sort(BrowserPageManager::compareByPriority);

    std::list<BrowserPage*>::const_iterator page_iter;
    for (page_iter = m_pageList.begin(); page_iter != m_pageList.end() && numPurged < maxToPurge; ++page_iter) 
    {
        BrowserPage* page = *page_iter;

        syslog(LOG_WARNING, "%s: purging page of priority: %d", __FUNCTION__, page->getPriority());

        // notify host that it needs to disconnect page
        BrowserServer::instance()->msgPurgePage(page->getProxy());

        // BrowserPage's destructor removes it from m_pageList

        numPurged++;
    }

    g_warning("Purged %d low priority pages out of %u.", numPurged, m_pageList.size());

    return numPurged;
}

/**
 * @brief BrowserPageManager determines ranking policy, knows the meaning of
 *        priority, so it should know how to compare a BrowserPage by priority.
 *
 */
bool
BrowserPageManager::compareByPriority(BrowserPage* b1, BrowserPage* b2)
{
    return (b1->getPriority() < b2->getPriority());
}

