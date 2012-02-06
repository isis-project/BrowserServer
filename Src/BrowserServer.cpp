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
#include <string>
#include <stdlib.h>
#include <errno.h>
#include <syslog.h>
#include <fstream>
#include <strings.h>
#include <time.h>
#include <assert.h>
#include <malloc.h>

#include <webkitpalmsettings.h>
#include <palmwebview.h>
#include <pbnjson.hpp>
#include <qpersistentcookiejar.h>

#include "BrowserCommon.h"
#include "BrowserPage.h"
#include "BrowserPageManager.h"
#include "BrowserServer.h"

#include "YapProxy.h"
#include "YapPacket.h"

#include <cjson/json.h>
#include "lunaservice.h"

#include "Utils.h"
#include <WebKitEventListener.h>
#include <BackupManager.h>
#include "PluginDirWatcher.h"
#include "Settings.h"
#include <QtNetwork>

#include "qwebkitplatformplugin.h"
#include "WebOSPlatformPlugin.h"

#include <cert_mgr.h>

#ifdef USE_HEAP_PROFILER
#include <google/heap-profiler.h>
#endif

static bool  gWebKitInit     = false;
static bool  __attribute__((unused)) gHeapProfilerOn = false;

static const int kTimerSecs = 15;

// Luna Service 

static LSMethod s_serviceMethods[] = {
    { "deleteImage",  BrowserServer::serviceCmdDeleteImage  },
    { "clearCache",   BrowserServer::serviceCmdClearCache   },
    { "clearCookies", BrowserServer::serviceCmdClearCookies },
#ifdef USE_HEAP_PROFILER
    { "dumpHeapProfile", BrowserServer::serviceCmdDumpHeapProfiler },
#endif
    { "getStats", BrowserServer::privateGetLunaStats },
    { "gc", BrowserServer::privateDoGc },
    { 0, 0},
};

static const char* const k_pszSimpleJsonSuccessResponse = "{\"returnValue\":true}";
static const char* const k_pszSimpleJsonFailureResponse = "{\"returnValue\":false}";

BrowserServer* BrowserServer::m_instance = NULL;
bool BrowserServer::isInternetConnectionAvailable = true;

template <class T>
bool ValidJsonObject(T jsonObj)
{
    return NULL != jsonObj && !is_error(jsonObj);
}


BrowserServer* BrowserServer::instance()
{
    if (m_instance == NULL) {
        new BrowserServer();
    }

    return m_instance;
}

BrowserServer::BrowserServer()
    : BrowserServerBase("browser")
    , m_pageCount(0)
    , m_networkAccessManager(0)
    , m_cookieJar(0)
    , m_service(0)
    , m_connectionManagerStatusToken(0)
    , m_wkEventListener(0)
    , m_pluginDirWatcher(0)
    , m_defaultDownloadDir()
#if defined(__arm__)
    , m_memchute(MEMCHUTE_NORMAL)
#endif
    , m_offscreenBackupBuffer(0)
    , m_offscreenBackupBufferLength(0)
    , m_comboBoxes(*this)
{

    QSettings settings;
    m_defaultDownloadDir = settings.value("DownloadPath").toString();

    m_pluginDirWatcher = new PluginDirWatcher();
    m_instance = this;

    m_networkAccessManager = new QNetworkAccessManager;

    m_cookieJar = new QPersistentCookieJar(m_networkAccessManager, "browser-app");
    m_networkAccessManager->setCookieJar(m_cookieJar);

    if (settings.value("CacheEnabled").toBool() && !settings.value("CachePath").toString().isEmpty()) {
        QNetworkDiskCache *diskCache = new QNetworkDiskCache (m_networkAccessManager);
        diskCache->setCacheDirectory(settings.value("CachePath").toString());
        diskCache->setMaximumCacheSize(StringToBytes(settings.value("CacheMaxSize").toString()));
        m_networkAccessManager->setCache(diskCache);
    }
}

BrowserServer::~BrowserServer()
{
    shutdownBrowserServer();
    delete m_pluginDirWatcher;
    m_instance = NULL;
    if (m_offscreenBackupBuffer) free(m_offscreenBackupBuffer);
}

bool BrowserServer::init() {
    return init(0,NULL);
}

bool BrowserServer::init(int argc,char ** argv) {

    QSettings settings;

    int rValue;
    if (0 != (rValue = (CertInitCertMgr("/etc/ssl/openssl.cnf")))) {
        g_critical("Unable to initialize certificate mgr: %d", rValue);
    }

    InitWebSettings();

    int ignore(0);
    int memTotal(0);

    int memFree  = getMemInfo(memTotal, ignore, ignore, ignore, ignore, ignore);
    if (memFree > 0 && memTotal >= 238) {
        webkitInit();
    }
    else {
        g_debug("Will initialize WebKit later.");
    }

    QString userInstalledPluginPath = settings.value("WebSettings/PluginSupplementalUserPath").toString();
    if (!userInstalledPluginPath.isEmpty()) {
        // create the directory if it doesn't exist, so it will be scanned
        // without needing to restart BS after a user installs a plugin.
        g_mkdir_with_parents(qPrintable(userInstalledPluginPath), 0755);
        if (!m_pluginDirWatcher->init(qPrintable(userInstalledPluginPath))) {
            g_warning("Unable to initialize plugin directory watcher");
        }
    }

    //TODO: a proper init return code; always success for now
    return true;
}

bool
BrowserServer::webKitInitialized() const
{
    return gWebKitInit;
}

void BrowserServer::initPlatformPlugin()
{
    QWebKitPlatformPlugin* plugin = QWebPage::platformPlugin();
    if (!plugin)
        return;

    QWebKitPlatformPlugin::Extension extension = (QWebKitPlatformPlugin::Extension)WebOSPlatformPlugin::SettingsExtension;
    if (!plugin->supportsExtension(extension))
        return;

    QScopedPointer<QObject> obj(plugin->createExtension(extension));
    if (!obj)
        return;

    WebOSPlatformPlugin::Settings* settings = qobject_cast<WebOSPlatformPlugin::Settings*>(obj.data());
    if (!settings)
        return;

    settings->setComboBoxFactory(&m_comboBoxes);
}

bool
BrowserServer::webkitInit()
{
    if (gWebKitInit)
        return true;

    g_debug("Initializing WebKit.");



    if (!m_instance->m_carrierCode.empty()) {
    }

    initPlatformPlugin();

    gWebKitInit = true;
    return true;
}

void
BrowserServer::clientConnected(YapProxy* proxy)
{
    BDBG("Client connected: %p", proxy);
    if (!webkitInit())
        return; // probably should refuse client connection here.
}

void
BrowserServer::clientDisconnected(YapProxy* proxy)
{
    BDBG("Client disconnected: %p", proxy);
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (pPage) {
        delete pPage;
        m_pageCount--;
        proxy->setPrivateData(0);
    }

#if defined(__arm__)
    // Exit -- we'll get relaunched, but this will cause us to free any
    // memory we leaked.
    if( !m_pageCount ) {
        g_message("Last client disconnected. Exiting BrowserServer.");
        shutdownBrowserServer();
        exit(0);
    }
#endif
}

void
BrowserServer::asyncCmdConnect(YapProxy* proxy, int32_t pageWidth, int32_t pageHeight, 
                               int32_t sharedBufferKey1, int32_t sharedBufferKey2,
                               int32_t sharedBufferSize, int32_t identifier)
{
    BDBG("Client connected: %p", proxy);

    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (pPage) {
        BERR("Client already send Connect command: %p", proxy);
        return;
    }

    // check if we are trying to pair this adapter with a BrowserPage instance
    pPage = BrowserPageManager::instance()->findInWatchedList(identifier);

    if (pPage != NULL) {  // attaching this BA to existing BP

        // remove it from watched list
        BrowserPageManager::instance()->removeFromWatchedList(identifier);

        pPage->attachToBuffer(pageWidth, pageHeight, sharedBufferKey1, sharedBufferKey2, sharedBufferSize);

        proxy->transferQueuedMessage(pPage->getProxy());
        pPage->setProxy(proxy);

        BrowserPageManager::instance()->registerPage(pPage);

    } else {  // making a new BP

        pPage = new BrowserPage(this, proxy, m_service);

        if (pPage == NULL) {
            g_error("Failed to allocate Browser Page");
            return;
        }

        if (!pPage->init(pageWidth, pageHeight, sharedBufferKey1, sharedBufferKey2, sharedBufferSize)) {
            g_warning("Failed to initialize Browser Page");
            return;
        }
    }

    m_pageCount++;

    proxy->setPrivateData(pPage);  
    BrowserPageManager::instance()->raisePagePriority(pPage);
}

void BrowserServer::asyncCmdDisconnect(YapProxy *proxy)
{
    proxy->setTerminate();
}

void
BrowserServer::asyncCmdInspectUrlAtPoint(YapProxy* proxy, int32_t queryNum, int32_t pointX, int32_t pointY)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    QWebHitTestResult hitResult = pPage->hitTest(pointX, pointY);
    QRect rect = hitResult.boundingRect();

    msgInspectUrlAtPointResponse(proxy, queryNum, !hitResult.linkUrl().isEmpty(),
        hitResult.linkUrl().toEncoded().constData(),
        qPrintable(hitResult.linkText()),
        rect.width(), rect.height(), rect.x(), rect.y());
}

void
BrowserServer::asyncCmdSetWindowSize(YapProxy* proxy, int32_t width, int32_t height)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    pPage->setWindowSize(width, height);
}

void BrowserServer::asyncCmdSetVirtualWindowSize(YapProxy *proxy, int32_t width, int32_t height)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    pPage->setVirtualWindowSize(width, height);
}

void
BrowserServer::asyncCmdSetUserAgent(YapProxy* proxy, const char* userAgent)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    pPage->setUserAgent(userAgent);
}

void
BrowserServer::asyncCmdOpenUrl(YapProxy* proxy, const char* url)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    pPage->openUrl(url);
    BrowserPageManager::instance()->raisePagePriority(pPage);
}

void
BrowserServer::asyncCmdSetHtml(YapProxy* proxy, const char* url, const char* body)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    if (!url || !body) {
        return;
    }
    pPage->setHTML(url, body);
}

void
BrowserServer::asyncCmdClickAt(YapProxy* proxy, int32_t contentX, int32_t contentY, int32_t numClicks, int32_t counter)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    bool isInteractive = pPage->isInteractiveAtPoint(contentX, contentY);

    pPage->clickAt(contentX, contentY, numClicks);

    // FIXME: Jesse says this is not fool-proof
    if (!isInteractive)
        msgClickRejected(proxy, counter);

    BrowserPageManager::instance()->raisePagePriority(pPage);
}

void
BrowserServer::asyncCmdHoldAt(YapProxy* proxy, int32_t contentX, int32_t contentY)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }
    pPage->holdAt(contentX, contentY);
    BrowserPageManager::instance()->raisePagePriority(pPage);
}

void
BrowserServer::asyncCmdEnableSelection(YapProxy* proxy, int32_t mouseX, int32_t mouseY)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    pPage->setSelectionMode(true);

    pPage->clickAt(mouseX, mouseY, 1);  // 1 = number of clicks

    pPage->setSelectionMode(false);

    msgRemoveSelectionReticle(proxy);
}

/**
 * Called on mouse up when in selection mode
 *
 */
void
BrowserServer::asyncCmdDisableSelection(YapProxy* proxy)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    pPage->setSelectionMode(false);
}

void
BrowserServer::asyncCmdKeyDown(YapProxy* proxy, uint16_t key, uint16_t modifiers)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    pPage->keyDown(key, modifiers);

    BrowserPageManager::instance()->raisePagePriority(pPage);
}

void
BrowserServer::asyncCmdKeyUp(YapProxy* proxy, uint16_t key, uint16_t modifiers)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    pPage->keyUp(key, modifiers);
}

void
BrowserServer::asyncCmdForward(YapProxy* proxy)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    pPage->pageForward();
    BrowserPageManager::instance()->raisePagePriority(pPage);
}

void
BrowserServer::asyncCmdBack(YapProxy* proxy)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    pPage->pageBackward();
    BrowserPageManager::instance()->raisePagePriority(pPage);
}

void
BrowserServer::asyncCmdReload(YapProxy* proxy)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    pPage->pageReload();
    BrowserPageManager::instance()->raisePagePriority(pPage);
}

void
BrowserServer::asyncCmdStop(YapProxy* proxy)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    pPage->pageStop();
    BrowserPageManager::instance()->raisePagePriority(pPage);
}

void
BrowserServer::asyncCmdPageFocused(YapProxy* proxy, bool focused)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    pPage->setFocus(focused);
    BrowserPageManager::instance()->setFocusedPage(pPage, focused);
}

bool BrowserServer::showComboBoxPopup(int id, const char* fileName)
{
    BrowserPage* page = BrowserPageManager::instance()->focusedPage();
    if (!page)
        return false;

    char idStr[20];
    ::snprintf(idStr, 20, "%d", id);
    msgPopupMenuShow(page->getProxy(), idStr, fileName);
    return true;
}

void BrowserServer::hideComboBoxPopup(int id)
{
    BrowserPage* page = BrowserPageManager::instance()->focusedPage();
    if (!page)
        return;

    char idStr[20];
    ::snprintf(idStr, 20, "%d", id);
    msgPopupMenuHide(page->getProxy(), idStr);
}

void
BrowserServer::asyncCmdExit(YapProxy* proxy)
{
    // Only for internal use. Exit so that we can get profiling dump for gcov
    shutdownBrowserServer();
    ::exit(0);
}


void BrowserServer::asyncCmdCancelDownload(YapProxy* proxy, const char* url)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");

        return;
    }

    pPage->downloadCancel(url);
    BrowserPageManager::instance()->raisePagePriority(pPage);
}

void BrowserServer::asyncCmdInterrogateClicks(YapProxy* proxy, bool enable)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");

        return;
    }

    pPage->enableInterrogateClicks(enable);
}

void BrowserServer::asyncCmdDragStart(YapProxy* proxy, int32_t contentX, int32_t contentY)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");

        return;
    }

    pPage->dragStart(contentX, contentY);
    BrowserPageManager::instance()->raisePagePriority(pPage);
}

void BrowserServer::asyncCmdDragProcess(YapProxy* proxy, int32_t deltaX, int32_t deltaY)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");

        return;
    }

    pPage->dragProcess(deltaX, deltaY);
    BrowserPageManager::instance()->raisePagePriority(pPage);
}

void BrowserServer::asyncCmdDragEnd(YapProxy* proxy, int32_t contentX, int32_t contentY)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");

        return;
    }

    pPage->dragEnd(contentX, contentY);
    BrowserPageManager::instance()->raisePagePriority(pPage);
}

void BrowserServer::asyncCmdSetMinFontSize(YapProxy* proxy, int32_t minFontSizePt)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    pPage->setMinFontSize(minFontSizePt);
}

void BrowserServer::asyncCmdFindString(YapProxy* proxy, const char* str, bool fwd)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    pPage->findString(str, fwd);

    BrowserPageManager::instance()->raisePagePriority(pPage);

    // FIXME: should return number of hits
}

void BrowserServer::asyncCmdSelectAll(YapProxy* proxy)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    pPage->selectAll(); 
}

void BrowserServer::asyncCmdPaste(YapProxy* proxy)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    pPage->paste();
}

void BrowserServer::asyncCmdCut(YapProxy* proxy)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    pPage->cut();
}

void BrowserServer::asyncCmdCopy(YapProxy* proxy, int queryNum)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    bool success = pPage->copy();

    msgCopySuccessResponse(proxy, queryNum, success);
}

void BrowserServer::asyncCmdClearSelection(YapProxy* proxy)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    pPage->clearSelection();
}

void BrowserServer::asyncCmdClearCache(YapProxy* proxy)
{
    webkitInit();
    clearCache();
}

void BrowserServer::asyncCmdClearCookies(YapProxy* proxy)
{
    webkitInit();
    m_cookieJar->clearCookies();
}

void BrowserServer::asyncCmdPopupMenuSelect(YapProxy* proxy, const char* identifier, int32_t selectedIdx)
{
    m_comboBoxes.selectItem(atoi(identifier), selectedIdx);
}


void BrowserServer::asyncCmdZoomSmartCalculateRequest(YapProxy* proxy, int32_t pointX, int32_t pointY)
{
    BrowserPage* pPage = static_cast<BrowserPage*>(proxy->privateData());
    if (pPage) {
        pPage->smartZoomCalculate( pointX, pointY );
        BrowserPageManager::instance()->raisePagePriority(pPage);
    }
    else {
        BERR("No page for this client.");
    }
}

void BrowserServer::asyncCmdSetEnableJavaScript(YapProxy* proxy, bool enable)
{
    // setting for current proxy's page
    BrowserPage* page = (BrowserPage*) proxy->privateData();
    if (!page) {
        BERR("javascript enable/disable : No page for this client.");
        return;
    }
    page->settingsJavaScriptEnabled(enable);
}

void BrowserServer::asyncCmdSetBlockPopups(YapProxy* proxy, bool block)
{
    QWebSettings::globalSettings()->setAttribute(QWebSettings::JavascriptCanOpenWindows, !block);
    BrowserPage* page = (BrowserPage*) proxy->privateData();
    if (!page) {
        BERR("No page for this client.");
        return;
    }
    page->settingsPopupsEnabled(!block);  // webkit uses "can open", inverse of blocked
}

void BrowserServer::asyncCmdSetAcceptCookies(YapProxy* proxy, bool enable) 
{
    m_cookieJar->enableCookies(enable);
}

void BrowserServer::asyncCmdSetShowClickedLink(YapProxy* proxy, bool enable) 
{
    BrowserPage* page = (BrowserPage*) proxy->privateData();
    if (!page) {
        BERR("No page for this client.");
        return;
    }
    page->setShowClickedLink(enable); 
}

void BrowserServer::asyncCmdGetInteractiveNodeRects(YapProxy* proxy, int32_t mouseX, int32_t mouseY)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    pPage->getInteractiveNodeRects(mouseX, mouseY);
}

void BrowserServer::asyncCmdMouseEvent(YapProxy* proxy, int32_t type, int32_t contentX, int32_t contentY, int32_t detail)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    pPage->mouseEvent(type, contentX, contentY, detail);
    BrowserPageManager::instance()->raisePagePriority(pPage);
}

void BrowserServer::asyncCmdGestureEvent(YapProxy* proxy, int32_t type, int32_t contentX, int32_t contentY, double scale, double rotate, int32_t centerX, int32_t centerY)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    pPage->gestureEvent(type, contentX, contentY, scale, rotate, centerX, centerY);   
    BrowserPageManager::instance()->raisePagePriority(pPage);
}

void BrowserServer::asyncCmdFreeze(YapProxy* proxy)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        return;
    }
    pPage->freeze();
}

void BrowserServer::asyncCmdThaw(YapProxy* proxy, int32_t sharedBufferKey1, int32_t sharedBufferKey2, int32_t sharedBufferSize)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        return;
    }

    pPage->thaw(sharedBufferKey1, sharedBufferKey2, sharedBufferSize); 
}

void BrowserServer::asyncCmdReturnBuffer(YapProxy* proxy, int32_t sharedBufferKey)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        return;
    }

    pPage->bufferReturned(sharedBufferKey);
}

void BrowserServer::asyncCmdSetScrollPosition(YapProxy* proxy, int32_t cx, int32_t cy, int32_t cw, int32_t ch)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        return;
    }
    pPage->setScrollPosition(cx, cy, cw, ch);
}

void BrowserServer::asyncCmdPluginSpotlightStart(YapProxy* proxy, int32_t cx, int32_t cy, int32_t cw, int32_t ch)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        return;
    }
    pPage->pluginSpotlightStart(cx, cy, cw, ch);
}

void BrowserServer::asyncCmdPluginSpotlightEnd(YapProxy* proxy)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        return;
    }
    pPage->pluginSpotlightEnd();
}

void BrowserServer::asyncCmdHideSpellingWidget(YapProxy* proxy)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        return;
    }
    pPage->hideSpellingWidget();
}

void BrowserServer::asyncCmdDisableEnhancedViewport(YapProxy* proxy,bool disable)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        return;
    }
    pPage->disableEnhancedViewport(disable);

}

void BrowserServer::syncCmdRenderToFile(YapProxy* proxy, const char* filename, int32_t viewX, int32_t viewY, int32_t viewW, int32_t viewH, int32_t& result)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        result = ENOMEM;
        return;
    }
    result = pPage->renderToFile(filename, viewX, viewY, viewW, viewH);
}

void BrowserServer::asyncCmdGetHistoryState(YapProxy* proxy, int32_t queryNum)
{
    BrowserPage* pPage = static_cast<BrowserPage*>(proxy->privateData());
    if (pPage) {
        msgGetHistoryStateResponse(proxy, queryNum, pPage->canGoBackward(), pPage->canGoForward());
    }
    else {
        BERR("No page for this client.");
    }
}

void BrowserServer::asyncCmdIsEditing(YapProxy* proxy, int32_t queryNum)
{
    BrowserPage* pPage = static_cast<BrowserPage*>(proxy->privateData());

    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    msgIsEditing(proxy, queryNum, pPage->isEditing());
}

void BrowserServer::asyncCmdInsertStringAtCursor(YapProxy* proxy, const char* str)
{
    BrowserPage* pPage = static_cast<BrowserPage*>(proxy->privateData());

    if (!pPage) {
        BERR("No page for this client");
        return;
    }

    pPage->insertStringAtCursor(str);
}

void BrowserServer::asyncCmdClearHistory(YapProxy* proxy)
{
    BrowserPage* pPage = static_cast<BrowserPage*>(proxy->privateData());
    if (pPage) {
        pPage->clearHistory();
    }
    else {
        BERR("No page for this client.");
    }
}


void BrowserServer::asyncCmdSetAppIdentifier(YapProxy* proxy, const char* identifier)
{
    BrowserPage* pPage = static_cast<BrowserPage*>(proxy->privateData());
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    pPage->setIdentifier(identifier);
}

void BrowserServer::asyncCmdAddUrlRedirect(YapProxy* proxy, const char* urlRe, int type, bool redirect, const char* userData)
{
    BrowserPage* pPage = static_cast<BrowserPage*>(proxy->privateData());
    if (pPage) {
        UrlMatchType redirType;
        switch (type) {
            case 0:
                redirType = UrlMatchRedirect;
                break;
            case 1:
                redirType = UrlMatchCommand;
                break;
            default:
                g_warning("Unknown redirect type: %d.", type);
                redirType = UrlMatchRedirect;
        }

        pPage->addUrlRedirect(urlRe, redirType, redirect, userData);
    }
    else {
        BERR("No page for this client.");
    }
}

void
BrowserServer::asyncCmdSaveImageAtPoint(YapProxy* proxy, int32_t queryNum, int32_t pointX, int32_t pointY, const char* saveDir)
{
    BrowserPage* pPage = static_cast<BrowserPage*>(proxy->privateData());
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    QString filepath (saveDir);
    if (filepath.isEmpty()) {
        filepath = m_defaultDownloadDir;
    }
    bool succeeded = pPage->saveImageAtPoint(pointX, pointY, filepath);
    if (!succeeded) {
        filepath.clear();
    }

    msgSaveImageAtPointResponse(proxy, queryNum, succeeded, qPrintable(filepath));
}

void
BrowserServer::asyncCmdGetImageInfoAtPoint(YapProxy* proxy, int32_t queryNum, int32_t pointX, int32_t pointY)
{
    BrowserPage* pPage = static_cast<BrowserPage*>(proxy->privateData());
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

   QWebHitTestResult hitResult = pPage->hitTest(pointX, pointY);
    bool succeeded = !hitResult.imageUrl().isEmpty();

    msgGetImageInfoAtPointResponse(proxy, queryNum, succeeded,
        hitResult.frame()->baseUrl().toEncoded().constData(),
        hitResult.imageUrl().toEncoded().constData(),
        qPrintable(hitResult.linkTitle().toString()),
        qPrintable(hitResult.alternateText()),
        hitResult.pixmap().width(),
        hitResult.pixmap().height(),
        "" /* FIXME: mimeType unkown */);
}

void
BrowserServer::asyncCmdIgnoreMetaTags(YapProxy* proxy,bool ignore)
{
    BrowserPage* pPage = static_cast<BrowserPage*>(proxy->privateData());
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }
    pPage->setIgnoreMetaRefreshTags(ignore);
    pPage->setIgnoreMetaViewport(ignore);

}

void
BrowserServer::asyncCmdSetNetworkInterface(YapProxy* proxy,const char* name)
{
    BrowserPage* pPage = static_cast<BrowserPage*>(proxy->privateData());
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }
    pPage->setNetworkInterface(name);

}

void
BrowserServer::asyncCmdIsInteractiveAtPoint(YapProxy* proxy, int32_t queryNum, int32_t pointX, int32_t pointY)
{
    BrowserPage* pPage = static_cast<BrowserPage*>(proxy->privateData());
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    msgIsInteractiveAtPointResponse(proxy, queryNum, pPage->isInteractiveAtPoint(pointX, pointY));
}

void
BrowserServer::asyncCmdGetElementInfoAtPoint(YapProxy* proxy, int32_t queryNum, int32_t pointX, int32_t pointY)
{
    BrowserPage* pPage = static_cast<BrowserPage*>(proxy->privateData());
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

#ifdef FIXME_QT
    Palm::ElementInfo info;
    bool succeeded = pPage->getElementInfoAtPoint(pointX, pointY, info);

    msgGetElementInfoAtPointResponse(proxy, queryNum, succeeded,
        info.element.empty() ? "" : info.element.c_str(),
        info.id.empty() ? "" : info.id.c_str(),
        info.name.empty() ? "" : info.name.c_str(),
        info.cname.empty() ? "" : info.cname.c_str(),
        info.type.empty() ? "" : info.type.c_str(),
        info.bounds.left, info.bounds.top, info.bounds.right, info.bounds.bottom, info.isEditable);
#endif // FIXME_QT
}

bool
BrowserServer::connectToMSMService()
{
    LSError error;
    LSErrorInit(&error);

    bool ret = LSCall(m_service,
        "palm://com.palm.lunabus/signal/addmatch", "{\"category\":\"/storaged\",\"method\":\"MSMStatus\",\"subscribe\":true}",
        msmStatusCallback, this, NULL, &error);
    if (!ret) {
        syslog(LOG_ERR, "Failed in calling palm://com.palm.lunabus/signal/addmatch: %s", error.message);
        LSErrorFree(&error);
    }

    return ret;
}

bool BrowserServer::msmStatusCallback(LSHandle *sh, LSMessage *message, void *ctx)
{
    const char* payload = LSMessageGetPayload(message);
    if (!message)
        return true;

    BrowserServer *bs = reinterpret_cast<BrowserServer*>(ctx);

    json_object* label = NULL;
    json_object* json = NULL;
    json_object* value = NULL;

    bool enteringMSMMode;

    label = 0;
    json = json_tokener_parse(payload);
    if (!json || is_error(json)) {
        return false;
    }

    value = json_object_object_get(json, "inMSM");
    if (ValidJsonObject(value)) {
        enteringMSMMode = json_object_get_boolean(value);
        if (enteringMSMMode) {
            g_message(" ENTERING MSM_MODE, shutting down plugin directory watcher");
            bs->m_pluginDirWatcher->suspend();
        }
        else {
            g_message(" EXITING MSM_MODE, restarting plugin directory watcher");
            bs->m_pluginDirWatcher->resume();
        }
    }

    json_object_put(json);
    return true;
}


bool
BrowserServer::connectToPrefsService()
{

    LSError error;
    LSErrorInit(&error);

    bool ret = LSCall(m_service,
        "palm://com.palm.systemservice/getPreferences", "{\"subscribe\":true, \"keys\": [ \"locale\", \"x_palm_carrier\", \"x_palm_textinput\" ]}",
        getPreferencesCallback, NULL, NULL, &error);
    if (!ret) {
        syslog(LOG_ERR, "Failed in calling palm://com.palm.systemservice/getPreferences: %s", error.message);
        LSErrorFree(&error);
    }

    return ret;


}

bool BrowserServer::getPreferencesCallback(LSHandle *sh, LSMessage *message, void *ctx)
{
    const char* payload = LSMessageGetPayload(message);
    if (!message)
        return true;

    json_object* label = NULL;
    json_object* json = NULL;
    json_object* value = NULL;

    const char* languageCode = NULL;
    const char* countryCode = NULL;
    std::string newLocale;

    label = 0;
    json = json_tokener_parse(payload);
    if (!json || is_error(json)) {
        return false;
    }

    value = json_object_object_get(json, "x_palm_carrier" );
    if( ValidJsonObject(value) )
    {
        char* carrierCode = json_object_get_string(value);
        if (gWebKitInit) {
        }
        if (m_instance != NULL) {
            m_instance->m_carrierCode = carrierCode;
        }
    }
    else
        g_message(" NO PALM CARRIER in PREFS DB" );

    value = json_object_object_get(json, "x_palm_textinput");
    if (ValidJsonObject(value)) {

        std::string strProp;

        json_object* prop = json_object_object_get(value, "spellChecking");
        if (ValidJsonObject(prop)) {

            strProp = json_object_get_string(prop);
#ifdef FIXME_QT
            if (strProp == "disabled")
                PalmBrowserSettings()->checkSpelling = WebKitPalmSettings::DISABLED;
            else if (strProp == "autoCorrect")
                PalmBrowserSettings()->checkSpelling = WebKitPalmSettings::AUTO_CORRECT;
            else if (strProp == "underline")
                PalmBrowserSettings()->checkSpelling = WebKitPalmSettings::UNDERLINE;
            else {
                g_warning("Unknown spellChecking flag: '%s'", strProp.c_str());
            }
#endif
        }

        prop = json_object_object_get(value, "grammarChecking");
        if (ValidJsonObject(prop)) {

            strProp = json_object_get_string(prop);
#ifdef FIXME_QT
            PalmBrowserSettings()->checkGrammar = strProp == "autoCorrect";
#endif
        }

        prop = json_object_object_get(value, "shortcutChecking");
        if (ValidJsonObject(prop)) {

            strProp = json_object_get_string(prop);
#ifdef FIXME_QT
            PalmBrowserSettings()->shortcutChecking = strProp == "autoCorrect";
#endif
        }
    }
    else {
        g_warning("Oops, no x_palm_textinput preference");
    }

    value = json_object_object_get(json, "locale");
    if ((value) && (!is_error(value))) {

        label = json_object_object_get(value, "languageCode");
        if ((label) || (!is_error(label))) {
            languageCode = json_object_get_string(label);
        }

        label = json_object_object_get(value, "countryCode");
        if ((label) || (!is_error(label))) {
            countryCode = json_object_get_string(label);
        }

        newLocale = languageCode;
        newLocale += "_";
        newLocale += countryCode;

        QLocale::setDefault(QLocale(QString::fromAscii(newLocale.c_str())));

        // may have cached pages that were loaded with old locale -- but if NOT
        // then we DO NOT want to dump the cache. We get this locale every time
        // BS starts up, so let's cache it in tmpfs so we don't have to keep
        // flushing the cache every single time we start up.
        if (gWebKitInit) {
            const char* kLocaleCacheFile = "/media/cryptofs/.cached-locale";
            gchar* buffer;
            gsize sz;
            if( TRUE == g_file_get_contents( kLocaleCacheFile, &buffer, &sz, 0 ) )
            {
                if( strcmp(buffer,newLocale.c_str() ) )
                {
                    // the locale HAS changed. flush the disk cache.
                    g_file_set_contents( kLocaleCacheFile, newLocale.c_str(), newLocale.size(), 0 );
                    instance()->clearCache();
                }
                g_free(buffer);
            }
            else
            {
                g_file_set_contents( kLocaleCacheFile, newLocale.c_str(), newLocale.size(), 0 );
                instance()->clearCache();
            }
        }
    }

    json_object_put(json);

    return true;
}

bool
BrowserServer::startService()
{
    LSError lsError;
    LSErrorInit(&lsError);

    bool result = LSRegister("com.palm.browserServer", &m_service, &lsError);
    if (!result)
        goto Exit;

    result = LSRegisterCategory(m_service, "/", s_serviceMethods, NULL, NULL, &lsError);
    if (!result)
        goto Exit;

    result = LSGmainAttach(m_service, mainLoop(), &lsError);

    if (result) {
        if (BackupManager::instance()->init(mainLoop(),m_service)) {
            m_instance->m_wkEventListener = new WebKitEventListener(BackupManager::instance());
        }
        else {
            syslog(LOG_ERR, "Unable to initialize backup manager.");
        }
        connectToPrefsService();
        connectToMSMService();
        registerForConnectionManager();
    }

Exit:
    if (!result) {
        g_message("%s: BrowserServer service failed to start, with error: %s",
                __FUNCTION__, lsError.message);
        LSErrorFree(&lsError);
        return false;
    }

    g_message("%s: BrowserServer started on service bus", __FUNCTION__);

#ifdef USE_HEAP_PROFILER
    HeapProfilerStart("browserserver");
#endif

    return true;
}

gboolean memWatchTimerCB(gpointer ptr)
{
    BrowserServer::instance()->doMemWatch();
    return true;
}

void BrowserServer::InitMemWatcher()
{
#ifdef FIXME_QT
    if (PalmBrowserSettings()->enableMemoryTracking) {
        g_warning("BrowserServer::InitMemWatcher - Configuring memory tracking.");
#if defined(__arm__)
        m_memchute = MEMCHUTE_NORMAL;
#endif
        GMainLoop* pLoop = BrowserServer::instance()->mainLoop();
        GSource* memWatchTimer = g_timeout_source_new_seconds (kTimerSecs);
        g_source_set_callback(memWatchTimer, memWatchTimerCB, NULL, NULL);
        g_source_attach(memWatchTimer, g_main_loop_get_context(pLoop));
    } else {
        g_warning("BrowserServer::InitMemWatcher - Memory tracking is disabled.");
    }
#endif
}

// Purpose: This function is called by a timer every kTimerSecs.  It is the
// agent that reacts to the changes in memchute levels updated by
// lowMemHandler()
void BrowserServer::doMemWatch()
{
    if (!gWebKitInit)
        return;

#if defined(__arm__)
    MemchuteThreshold threshold = m_memchute;
    static int counter = 0;
    static const int kCounterIntervalForCleanup = 4;

    counter++;
    if (counter >= kCounterIntervalForCleanup) {
        counter = 0;

    }

    switch(threshold) {
    case MEMCHUTE_NORMAL:
        // In acceptable levels, just do opportunistic GCs to keep pressure low.
        break;

    case MEMCHUTE_MEDIUM:
        // Throttle these calls to once per second to minimize chance of
        // thrashing in GC.
        if (counter == 1) {
            // Clear caches but don't kill pages.
            malloc_trim(0);
        }
        break;

    case MEMCHUTE_LOW:
    case MEMCHUTE_CRITICAL:
    case MEMCHUTE_REBOOT: {
            // Throttle these calls to once per second to minimize chance of
            // thrashing in GC.
            if (counter == 1) {
                g_warning("BrowserServer::doMemWatch - Taking low memory actions");
                BrowserPageManager::instance()->purgeLowPriorityPages();
                malloc_trim(0);
            }

            if (threshold == MEMCHUTE_LOW)
                break;

            // For MEMCHUTE_CRITICAL and MEMCHUTE_REBOOT only.
            int numPages = BrowserPageManager::instance()->numPages();
            // If we are in CRITICAL/REBOOT and there is only one page open,
            // exit and allow the browser server to restart.  If the
            // page is not in the foreground, this is the same
            // behavior as calling purgeLowPriorityPages as the page
            // would be dropped and the browser server would restart.
            // If the page is in the foreground, the purge would fail
            // so just exit.  The page will get a spinner while the
            // browser server restarts and then it will reload.
            if ((MEMCHUTE_LOW != m_memchute) && (1 == numPages)) {
                g_warning("doMemWatch - Only one page left - exiting.");
                shutdownBrowserServer();
                exit(0);
            }
        }
        break;

    default:
        break;
    }
#endif  // __arm__
}

int BrowserServer::getMemInfo(int& memTotal, int& memFree, int& swapTotal,
                              int& swapFree, int& cached, int& swapCached)
{
    static const std::string sMemTotal("MemTotal");
    static const std::string sMemFree("MemFree");
    static const std::string sSwapTotal("SwapTotal");
    static const std::string sSwapFree("SwapFree");
    static const std::string sCached("Cached");
    static const std::string sSwapCached("SwapCached");
    static const std::string sKBLabel("kb");
    static const std::string sMBLabel("mb");

    std::ifstream memInfo("/proc/meminfo");
    if (!memInfo) {
        return -1;
    }
    std::string field;
    std::string label;
    int value;

    while(memInfo >> field) {
        // strip off the ':' on the end of each label
        field = field.substr(0, field.length() - 1);
        memInfo >> value;
        memInfo >> label;

        // we want memory in terms of megabytes
        // so divide by 1024 if the label is for kilobytes
        // and then if it's not megabytes, presume it's in bytes so
        // divide by 1024 * 1024
        if (!strcasecmp(label.c_str(), sKBLabel.c_str())) {
            value /= 1024;
        } else if (strcasecmp(label.c_str(), sMBLabel.c_str())) {
            value /= 1024 * 1024;
        }

        if (field == sMemTotal) {
            memTotal = value;
        } else if (field == sMemFree) {
            memFree = value;
        } else if (field == sSwapTotal) {
            swapTotal = value;
        } else if (field == sSwapFree) {
            swapFree = value;
        } else if (field == sCached) {
            cached = value;
        } else if (field == sSwapCached) {
            swapCached = value;
        }
    }
    memInfo.close();
    return memFree;
}

/**
 * @brief Memchute callback that is passed BrowserServer's current memory
 *        threshold level (NORMAL, LOW, CRITICAL)
 *
 * @todo  Memchute will be modified in the near future to allow callbacks
 *        with void* args, so we can pass an instance of BrowserServer for
 *        a less destructive response.
 *
 * @author Anthony D'Auria
 */
#if defined(__arm__)

static const char* MemchuteThresholdName(MemchuteThreshold threshold)
{
    static char buff[48];
    switch (threshold) {
        case MEMCHUTE_NORMAL:   return "MEMCHUTE_NORMAL";
        case MEMCHUTE_MEDIUM:   return "MEMCHUTE_MEDIUM";
        case MEMCHUTE_LOW:      return "MEMCHUTE_LOW";
        case MEMCHUTE_CRITICAL: return "MEMCHUTE_CRITICAL";
        case MEMCHUTE_REBOOT:   return "MEMCHUTE_REBOOT";
        default: {
            snprintf(buff, G_N_ELEMENTS(buff), "<unknown (%d)>", int(threshold));
            return buff;
        }
    }
}

// Purpose: This handler is called back in response to memchute notifications.
// It is used to update the memchute level.
void
BrowserServer::handleMemchuteNotification(MemchuteThreshold threshold)
{
    BrowserServer::instance()->m_memchute = threshold;
    g_warning("Received %s", MemchuteThresholdName(threshold));
}


#endif // __arm__ 

void
BrowserServer::stopService() 
{
    LSError lsError;
    LSErrorInit(&lsError);

    bool result = LSUnregister(m_service, &lsError);
    if (!result)
        LSErrorFree(&lsError);

    m_service = NULL;
}

bool
BrowserServer::serviceCmdDeleteImage(LSHandle *lsHandle, LSMessage *message, void *ctx) 
{
    LSError lserror;
    LSErrorInit(&lserror);

    struct json_object* root;
    struct json_object* label;

    std::string fileName; // filename argument
    size_t suffixPos; // for filename validation

    std::string errText; // for response
    bool success = false;

    const char* msgPayload = LSMessageGetPayload(message);
    if (msgPayload == NULL) {
        return false;
    }

    root = json_tokener_parse(msgPayload);
    if (is_error(root)) {
        root = NULL;
        goto Exit;
    }

    label = json_object_object_get(root, "file");
    if (label == NULL || is_error(label)) {
        g_message("%s: BrowserServer failed to find parameter 'file' in message", __FUNCTION__);
        errText = "file parameter not given";
        goto Exit;
    }

    fileName = json_object_get_string(label);

    // checking if it ends in .png 
    suffixPos = fileName.rfind(".png");
    if (suffixPos == std::string::npos || (fileName.length()-4 != suffixPos)) {
        g_message("%s: invalid filename '%s'", __FUNCTION__, fileName.c_str());
        errText = "invalid filename '" + fileName + "'";
        goto Exit;
    }

    if (::unlink(fileName.c_str()) < 0 ) {
        g_message("%s: unable to unlink filename '%s', errno: %d",
                __FUNCTION__, fileName.c_str(), errno);
        errText = "unable to unlink file '" + fileName + "'";
    } else {
        success = true;
    }

Exit:
    if (root != NULL && !is_error(root)) {
        json_object_put(root);
    }

    // construct response json string
    json_object* replyJson = json_object_new_object();
    json_object_object_add(replyJson, 
            (char*) "returnValue",
            json_object_new_boolean(success));

    if (!success) {
        json_object_object_add(replyJson, 
                (char*) "errorCode",
                json_object_new_int(-1));

        json_object_object_add(replyJson, 
                (char*) "errorText",
                json_object_new_string(errText.c_str()));
    }

    // send response
    if (!LSMessageReply(lsHandle, message, json_object_to_json_string(replyJson), &lserror)) {
        LSErrorFree(&lserror);
    }

    json_object_put(replyJson);

    return true;
}

bool
BrowserServer::serviceCmdClearCache(LSHandle *lsHandle, LSMessage *message, void *ctx) 
{
    bool success = false;
    if (instance() && instance()->webkitInit()) {
        instance()->clearCache();
        success = true;
    }

    // send response
    LSError lsError;
    LSErrorInit(&lsError);
    const char* const response = success ? k_pszSimpleJsonSuccessResponse : k_pszSimpleJsonFailureResponse;

    if (!LSMessageReply(lsHandle, message, response, &lsError)) {
        LSErrorFree(&lsError);
    }

    return true;
}

bool
BrowserServer::serviceCmdClearCookies(LSHandle *lsHandle, LSMessage *message, void *ctx) 
{
    bool success = false;
    if (instance() && instance()->webkitInit()) {
        instance()->m_cookieJar->clearCookies();
        success = true;
    }

    // send response
    LSError lsError;
    LSErrorInit(&lsError);
    const char* const response = success ? k_pszSimpleJsonSuccessResponse : k_pszSimpleJsonFailureResponse;

    if (!LSMessageReply(lsHandle, message, response, &lsError)) {
        LSErrorFree(&lsError);
    }

    return true;
}
#ifdef USE_HEAP_PROFILER

bool
BrowserServer::serviceCmdDumpHeapProfiler(LSHandle* lsHandle, LSMessage *message, void *ctx)
{
    HeapProfilerDump("period");
    return true;
}

#endif  // USE_HEAP_PROFILER


gboolean BrowserServer::postStats(gpointer ctxt)
{
    LSError lsError;
    std::string jsonStr;
    std::string counters;
    std::multimap<std::string,std::string> docMap;

    return TRUE;

    LSErrorInit(&lsError);


    jsonStr = "{ ";

    // assemble the documents array:
    jsonStr += " \"documents\": [";

    int index = 0;
    std::multimap<std::string,std::string>::const_iterator it;
    for (it=docMap.begin(), index = 0; it != docMap.end(); ++it, ++index) {

        if (index != 0)
            jsonStr += ", ";

        jsonStr += "{ ";
        jsonStr +=  it->second;
        jsonStr += " }";
    }

    jsonStr += " ],\n";

    // assemble the counters frame:
    jsonStr += " \"counters\": {";
    jsonStr += counters;
    jsonStr += " } ";

    jsonStr += " }";

    if (!LSSubscriptionPost(BrowserServer::instance()->m_service, "/", "getStats", jsonStr.c_str(), &lsError))
        LSErrorFree (&lsError);

    return TRUE;
}


void BrowserServer::initiateStatsReporting()
{
    static GSource* src = 0;
    if (!src) {
        static const int kStatsReportingIntervalSecs = 5;

        src = g_timeout_source_new_seconds(kStatsReportingIntervalSecs);
        g_source_set_callback(src, BrowserServer::postStats, NULL, NULL);
        g_source_attach(src, g_main_loop_get_context(BrowserServer::instance()->mainLoop()));
        g_source_unref(src);
    }
}


bool
BrowserServer::privateGetLunaStats(LSHandle* handle, LSMessage* message, void* ctxt)
{
    bool ret(false);
    LSError lsError;
    bool subscribed = false;
    std::string jsonStr;
    std::string counters;
    std::multimap<std::string,std::string> docMap;

    LSErrorInit(&lsError);

    jsonStr = "{ ";

    if (!message) {
        ret = false;
        goto Done;
    }

    if (LSMessageIsSubscription(message)) {

        ret = LSSubscriptionProcess(handle, message, &subscribed, &lsError);
        if (!ret) {
            LSErrorFree(&lsError);
            goto Done;
        }
    }

    if (subscribed)
        BrowserServer::instance()->initiateStatsReporting();

Done:

    jsonStr += "\"returnValue\":";
    jsonStr += ret ? "true" : "false";
    jsonStr += ", ";

    jsonStr += "\"subscribed\":";
    jsonStr += subscribed ? "true" : "false";
    jsonStr += " }";

    if (!LSMessageReply(handle, message, jsonStr.c_str(), &lsError))
        LSErrorFree(&lsError);

    return true;
}


bool
BrowserServer::privateDoGc(LSHandle* handle, LSMessage* message, void* ctxt)
{
    return true;
}


void BrowserServer::registerForConnectionManager()
{
    LSError error;
    LSErrorInit(&error);

    bool ret = LSCall(m_service, "palm://com.palm.lunabus/signal/registerServerStatus",
                      "{\"serviceName\":\"com.palm.connectionmanager\"}",
                      connectionManagerConnectCallback, this, &m_connectionManagerStatusToken,
                      &error);
    if (!ret) {
        g_critical("Failed in calling palm://com.palm.lunabus/signal/registerServerStatus: %s",
                   error.message);
        LSErrorFree(&error);
        return;
    }
}

bool BrowserServer::connectionManagerConnectCallback(LSHandle *sh, LSMessage *message, void *ctx)
{
    if (!message)
        return true;

    const char* payload = LSMessageGetPayload(message);
    json_object* label = 0;
    json_object* json = 0;
    bool connected = false;

    label = 0;
    json = json_tokener_parse(payload);
    if (!json || is_error(json))
        goto Done;

    label = json_object_object_get(json, "connected");
    if (!label || is_error(label))
        goto Done;
    connected = json_object_get_boolean(label);

    if (connected) {

        // We are connected to the systemservice. call and get the connection manager status        
        BrowserServer* bs = BrowserServer::instance();

        bool ret = false;
        LSError error;
        LSErrorInit(&error);

        ret = LSCall(bs->m_service,
                     "palm://com.palm.connectionmanager/getstatus", "{\"subscribe\":true}",
                     connectionManagerGetStatusCallback, bs, NULL, &error);
        if (!ret) {
            g_critical("Failed in calling palm://com.palm.systemservice/getstatus: %s",
                       error.message);
            LSErrorFree(&error);
        }
    }

Done:

    if (json && !is_error(json))
        json_object_put(json);

    return true;
}

bool BrowserServer::connectionManagerGetStatusCallback(LSHandle* sh, LSMessage* message, void* ctxt)
{
    if (!message)
        return true;

    const char* payload = LSMessageGetPayload(message); 
    json_object* label = 0;
    json_object* json = 0;
    std::string wifiIpAddress;
    std::string wanIpAddress;
    std::string selectedIpAddress;

    BrowserServer* bs = BrowserServer::instance();

    json = json_tokener_parse(payload);
    if (!json || is_error(json)) {
        return false;
    }

    label = json_object_object_get(json, "isInternetConnectionAvailable");
    if (label && !is_error(label)) {
        isInternetConnectionAvailable = json_object_get_boolean(label);
    }

    label = json_object_object_get(json, (char*) "wifi");
    if (label && !is_error(label)) {

        json_object* l = json_object_object_get(label, (char*) "ipAddress");
        if (l && !is_error(l)) {
            wifiIpAddress = json_object_get_string(l);
        }
    }

    label = json_object_object_get(json, (char*) "wan");
    if (label && !is_error(label)) {

        json_object* l = json_object_object_get(label, (char*) "ipAddress");
        if (l && !is_error(l)) {
            wanIpAddress = json_object_get_string(l);
        }
    }

    json_object_put(json);

    // Prefer WIFI over WAN
    if (!wanIpAddress.empty())
        selectedIpAddress = wanIpAddress;

    if (!wifiIpAddress.empty())
        selectedIpAddress = wifiIpAddress;


    if (!selectedIpAddress.empty() && selectedIpAddress != bs->m_ipAddress) {

        g_message("IP address changed: %s. Restarting network", selectedIpAddress.c_str());

        bs->m_ipAddress = selectedIpAddress;

        // Restart networking only if webkit has been initialized
        if (gWebKitInit) {
        }
    }

    return true;
}

void
BrowserServer::asyncCmdSetMouseMode(YapProxy* proxy, int32_t mode)
{
    BrowserPage* pPage = static_cast<BrowserPage*>(proxy->privateData());
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    pPage->setMouseMode(static_cast<Palm::MouseMode>(mode));
}

void BrowserServer::asyncCmdHitTest(YapProxy *proxy, int32_t queryNum, int32_t cx, int32_t cy)
{
    BrowserPage *pPage = static_cast<BrowserPage *>(proxy->privateData());
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    pbnjson::JValue obj = pbnjson::Object();
    obj.put("x", cx);
    obj.put("y", cy);

    QWebHitTestResult hitResult = pPage->hitTest(cx, cy);
    if(!hitResult.isNull()) {

        QWebElement resultElement = hitResult.element();
        if (resultElement.isNull())
            resultElement = hitResult.linkElement().isNull() ? hitResult.enclosingBlockElement() : hitResult.linkElement();

        if (!resultElement.isNull()) {

            obj.put("isNull", false);
            obj.put("element", qPrintable(resultElement.tagName().toLower()));
            obj.put("editable", hitResult.isContentEditable());

            int sx = pPage->getPageX();
            int sy = pPage->getPageY();
            double z = pPage->getZoomLevel();
            QRect bounds = hitResult.boundingRect();

            pbnjson::JValue boundsObj = pbnjson::Object();
            boundsObj.put("left", ::round((bounds.x() - sx) * z));
            boundsObj.put("top", ::round((bounds.y() - sy) * z));
            boundsObj.put("right", ::round((bounds.x() + bounds.width() - sx) * z));
            boundsObj.put("bottom", ::round((bounds.y() + bounds.height() - sy) * z));

            obj.put("bounds", boundsObj);
        }

        if (!hitResult.linkUrl().isEmpty()) {
            obj.put("isLink", true);
            obj.put("linkUrl", hitResult.linkUrl().toEncoded().constData());
            obj.put("linkText", qPrintable(hitResult.linkText()));
        }

        if (!hitResult.imageUrl().isEmpty()) {
            obj.put("isImage", true);
            obj.put("altText", qPrintable(hitResult.alternateText()));
            obj.put("imageUrl", hitResult.imageUrl().toEncoded().constData());
        }
    }

    pbnjson::JGenerator serializer(NULL);
    std::string json;
    pbnjson::JSchemaFile schema("/etc/palm/browser/HitTest.schema");
    if (!serializer.toString(obj, schema, json)) {
        BERR("Error generating JSON");
    } else {
        BDBG("Generated JSON:\n %s\n", json.c_str());
        msgHitTestResponse(proxy, queryNum, json.c_str());
    }
}

unsigned char* BrowserServer::getOffscreenBackupBuffer(int bufferSize) {
       if (bufferSize<=0) return 0;
       if (m_offscreenBackupBufferLength<bufferSize) {
               if (m_offscreenBackupBuffer) {
                       free(m_offscreenBackupBuffer);
               }
               m_offscreenBackupBuffer = (unsigned char*)malloc(bufferSize);
               if (m_offscreenBackupBuffer) m_offscreenBackupBufferLength = bufferSize;
       }
       return m_offscreenBackupBuffer;
}

void BrowserServer::asyncCmdPrintFrame(YapProxy* proxy, const char* frameName, int lpsJobId, int width, int height, int dpi, bool landscape, bool reverseOrder)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");

        return;
    }

    pPage->printFrame(frameName, lpsJobId, width, height, dpi, landscape, reverseOrder);
}

void BrowserServer::asyncCmdTouchEvent(YapProxy* proxy, int32_t type, int32_t touchCount, int32_t modifiers, const char* touchesJson)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }
    pPage->touchEvent(type, touchCount, modifiers, touchesJson);
}

void
BrowserServer::asyncCmdGetTextCaretBounds(YapProxy* proxy, int32_t queryNum)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }
    int left, top, right, bottom;
    left = top = right = bottom = 0;
    pPage->getTextCaretBounds(left, top, right, bottom);
    msgGetTextCaretBoundsResponse(proxy, queryNum, left, top, right, bottom);
}

void BrowserServer::asyncCmdSetZoomAndScroll(YapProxy* proxy, double zoom, int32_t cx, int32_t cy)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    pPage->setZoomAndScroll(zoom, cx, cy);
}

void BrowserServer::asyncCmdScrollLayer(YapProxy* proxy, int32_t id, int32_t deltaX, int32_t deltaY)
{
    BrowserPage* pPage = (BrowserPage*) proxy->privateData();
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    pPage->scrollLayer(id, deltaX, deltaY);
}

void
BrowserServer::asyncCmdSetDNSServers(YapProxy* proxy, const char *servers)
{
    BrowserPage *pPage = static_cast<BrowserPage*>(proxy->privateData());
    if (!pPage) {
        BERR("No page for this client.");
        return;
    }

    BDBG("Set DNS Servers: %s", servers);
    pPage->setDNSServers(servers);
}

void BrowserServer::shutdownBrowserServer()
{
    delete m_networkAccessManager;
    m_networkAccessManager = 0;
    m_cookieJar = 0;
}

void BrowserServer::clearCache()
{
    QWebSettings::clearMemoryCaches ();

    if (m_networkAccessManager->cache())
        m_networkAccessManager->cache()->clear();
}
