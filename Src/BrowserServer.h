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

#ifndef BROWSERSERVER_H
#define BROWSERSERVER_H

#include <list>

#include "CmdResourceHandlers.h"
#include <BrowserServerBase.h>
#include <lunaservice.h>
#include <QtCore/QString>
#include "BrowserComboBox.h"

#include <glib.h>  // memchute doesn't include it currently

#if defined(__arm__)
extern "C" {
#include <memchute.h>
}
#endif // __arm__

class WebKitEventListener;
class PluginDirWatcher;
class QNetworkAccessManager;
class QPersistentCookieJar;


class BrowserServer : public BrowserServerBase, private BrowserComboBoxServer {
public:

    static BrowserServer* instance();
    static bool isInternetConnectionAvailable;

    bool init();
    bool init(int argc,char **argv);

    bool webkitInit();
    bool webKitInitialized() const;

    int m_pageCount;

    virtual void clientConnected(YapProxy* proxy);
    virtual void clientDisconnected(YapProxy* proxy);

    QNetworkAccessManager *networkAccessManager() { return m_networkAccessManager; }

    // Luna Service Commands
    static bool serviceCmdDeleteImage(LSHandle *lsHandle, LSMessage *message, void *ctx);
    static bool serviceCmdClearCache(LSHandle *lsHandle, LSMessage *message, void *ctx);
    static bool serviceCmdClearCookies(LSHandle *lsHandle, LSMessage *message, void *ctx);
#ifdef USE_HEAP_PROFILER
    static bool serviceCmdDumpHeapProfiler(LSHandle* lsHandle, LSMessage *message, void *ctx);
#endif
    static bool privateDoGc(LSHandle* handle, LSMessage* message, void* ctxt);

    bool        startService();
    void        stopService();

    void InitMemWatcher();
    void doMemWatch();

#if defined(__arm__)
    static void handleMemchuteNotification(MemchuteThreshold threshold);
#endif

    unsigned char* getOffscreenBackupBuffer(int bufferSize);
    LSHandle* getServiceHandle() const { return m_service; }
    void shutdownBrowserServer();

private:

    BrowserServer();
    ~BrowserServer();

    BrowserServer(const BrowserServer&);
    BrowserServer& operator=(const BrowserServer&);

    static BrowserServer* m_instance;

    QNetworkAccessManager* m_networkAccessManager;
    QPersistentCookieJar *m_cookieJar;

    LSHandle* m_service;
    LSMessageToken m_connectionManagerStatusToken;
    std::string m_ipAddress;
    WebKitEventListener* m_wkEventListener;
    std::string m_carrierCode;

    bool connectToMSMService();
    static bool msmStatusCallback(LSHandle *sh, LSMessage *message, void *ctx);

    bool connectToPrefsService();
    static bool getPreferencesCallback(LSHandle *sh, LSMessage *message, void *ctx);

    int getMemInfo(int& memTotal, int& memFree, int& swapTotal, int& swapFree,
                   int& cached, int& swapCached);
    PluginDirWatcher* m_pluginDirWatcher;

    QString m_defaultDownloadDir;

#if defined(__arm__)
    MemchuteThreshold m_memchute;
#endif  // __arm__
    unsigned char* m_offscreenBackupBuffer;
    int m_offscreenBackupBufferLength;

    BrowserComboBoxList m_comboBoxes;

    void registerForConnectionManager();

    static bool connectionManagerConnectCallback(LSHandle *sh, LSMessage *message, void *ctx); 
    static bool connectionManagerGetStatusCallback(LSHandle* sh, LSMessage* message, void* ctxt);

    // Async Commands
    virtual void asyncCmdConnect(YapProxy* proxy, int32_t pageWidth, int32_t pageHeight, int32_t sharedBufferKey1, int32_t sharedBufferKey2, int32_t sharedBufferSize, int32_t identifier);
    virtual void asyncCmdSetWindowSize(YapProxy* proxy, int32_t width, int32_t height);
    virtual void asyncCmdSetVirtualWindowSize(YapProxy* proxy, int32_t width, int32_t height);
    virtual void asyncCmdSetUserAgent(YapProxy* proxy, const char* userAgent);
    virtual void asyncCmdOpenUrl(YapProxy* proxy, const char* url);
    virtual void asyncCmdSetHtml(YapProxy* proxy, const char* url, const char* body);
    virtual void asyncCmdClickAt(YapProxy* proxy, int32_t contentX, int32_t contentY, int32_t numClicks, int32_t counter);
    virtual void asyncCmdHoldAt(YapProxy* proxy, int32_t contentX, int32_t contentY);
    virtual void asyncCmdKeyDown(YapProxy* proxy, uint16_t key, uint16_t modifiers);
    virtual void asyncCmdKeyUp(YapProxy* proxy, uint16_t key, uint16_t modifiers);
    virtual void asyncCmdForward(YapProxy* proxy);
    virtual void asyncCmdBack(YapProxy* proxy);
    virtual void asyncCmdReload(YapProxy* proxy);
    virtual void asyncCmdStop(YapProxy* proxy);
    virtual void asyncCmdPageFocused(YapProxy* proxy, bool focused);
    virtual void asyncCmdExit(YapProxy* proxy);
    virtual void asyncCmdCancelDownload(YapProxy* proxy, const char* url);
    virtual void asyncCmdInterrogateClicks(YapProxy* proxy, bool enable);
    virtual void asyncCmdZoomSmartCalculateRequest(YapProxy* proxy, int32_t pointX, int32_t pointY);
    virtual void asyncCmdDragStart(YapProxy* proxy, int32_t contentX, int32_t contentY);
    virtual void asyncCmdDragProcess(YapProxy* proxy, int32_t deltaX, int32_t deltaY);
    virtual void asyncCmdDragEnd(YapProxy* proxy, int32_t contentX, int32_t contentY);
    virtual void asyncCmdSetMinFontSize(YapProxy* proxy, int32_t minFontSizePt);
    virtual void asyncCmdFindString(YapProxy* proxy, const char* str, bool fwd);
    virtual void asyncCmdEnableSelection(YapProxy* proxy, int32_t mouseX, int32_t mouseY);
    virtual void asyncCmdDisableSelection(YapProxy* proxy);
    virtual void asyncCmdClearSelection(YapProxy* proxy);
    virtual void asyncCmdSelectAll(YapProxy* proxy);
    virtual void asyncCmdCut(YapProxy* proxy);
    virtual void asyncCmdCopy(YapProxy* proxy, int queryNum);
    virtual void asyncCmdPaste(YapProxy* proxy);
    virtual void asyncCmdClearCache(YapProxy* proxy);
    virtual void asyncCmdClearCookies(YapProxy* proxy);
    virtual void asyncCmdPopupMenuSelect(YapProxy* proxy, const char* identifier, int32_t selectedIdx);
    virtual void asyncCmdSetEnableJavaScript(YapProxy* proxy, bool enable);
    virtual void asyncCmdSetBlockPopups(YapProxy* proxy, bool enable);
    virtual void asyncCmdSetAcceptCookies(YapProxy* proxy, bool enable);
    virtual void asyncCmdSetShowClickedLink(YapProxy* proxy, bool enable);
    virtual void asyncCmdMouseEvent(YapProxy* proxy, int32_t type, int32_t contentX, int32_t contentY, int32_t detail);
    virtual void asyncCmdGestureEvent(YapProxy* proxy, int32_t type, int32_t contentX, int32_t contentY, double scale, double rotate, int32_t centerX, int32_t centerY);
    virtual void asyncCmdDisconnect(YapProxy *proxy);
    virtual void asyncCmdInspectUrlAtPoint(YapProxy* proxy, int32_t queryNum, int32_t pointX, int32_t pointY);
    virtual void asyncCmdGetHistoryState(YapProxy* proxy, int32_t queryNum);
    virtual void asyncCmdIsEditing(YapProxy* proxy, int32_t queryNum);
    virtual void asyncCmdClearHistory(YapProxy* proxy);
    virtual void asyncCmdSetAppIdentifier(YapProxy* proxy, const char* identifier);
    virtual void asyncCmdAddUrlRedirect(YapProxy* proxy, const char* urlRe, int type, bool redirect, const char* userData);

    virtual void asyncCmdGetInteractiveNodeRects(YapProxy* proxy, int32_t mouseX, int32_t mouseY);

    virtual void asyncCmdInsertStringAtCursor(YapProxy* proxy, const char* text);

    virtual void asyncCmdSaveImageAtPoint(YapProxy* proxy, int32_t queryNum, int32_t pointX, int32_t pointY, const char* saveDir);
    virtual void asyncCmdGetImageInfoAtPoint(YapProxy* proxy, int32_t queryNum, int32_t pointX, int32_t pointY);
    virtual void asyncCmdIsInteractiveAtPoint(YapProxy* proxy, int32_t queryNum, int32_t pointX, int32_t pointY);
    virtual void asyncCmdGetElementInfoAtPoint(YapProxy* proxy, int32_t queryNum, int32_t pointX, int32_t pointY);
    virtual void asyncCmdSetMouseMode(YapProxy* proxy, int32_t mode);
    virtual void asyncCmdSetScrollPosition(YapProxy* proxy, int32_t cx, int32_t cy, int32_t cw, int32_t ch);
    virtual void asyncCmdPluginSpotlightStart(YapProxy* proxy, int32_t cx, int32_t cy, int32_t cw, int32_t ch);
    virtual void asyncCmdPluginSpotlightEnd(YapProxy* proxy);
    virtual void syncCmdRenderToFile(YapProxy* proxy, const char* filename, int32_t viewX, int32_t viewY, int32_t viewW, int32_t viewH, int32_t& result);
    virtual void asyncCmdHideSpellingWidget(YapProxy* proxy);
    virtual void asyncCmdDisableEnhancedViewport(YapProxy* proxy,bool disable);
    virtual void asyncCmdIgnoreMetaTags(YapProxy* proxy,bool ignore);
    virtual void asyncCmdSetNetworkInterface(YapProxy* proxy,const char* name);
    virtual void asyncCmdHitTest(YapProxy* proxy, int32_t queryNum, int32_t cx, int32_t cy);
    virtual void asyncCmdPrintFrame(YapProxy* proxy, const char* frameName, int32_t lpsJobId, int32_t width, int32_t height, int32_t dpi, bool landscape, bool reverseOrder);
    virtual void asyncCmdTouchEvent(YapProxy* proxy, int32_t type, int32_t touchCount, int32_t modifiers, const char* touchesJson);
    virtual void asyncCmdGetTextCaretBounds(YapProxy* proxy, int32_t queryNum);
    virtual void asyncCmdFreeze(YapProxy* proxy);
    virtual void asyncCmdThaw(YapProxy* proxy, int32_t sharedBufferKey1, int32_t sharedBufferKey2, int32_t sharedBufferSize);
    virtual void asyncCmdReturnBuffer(YapProxy* proxy, int32_t sharedBufferKey);
    virtual void asyncCmdSetZoomAndScroll(YapProxy* proxy, double zoom, int32_t cx, int32_t cy);
    virtual void asyncCmdScrollLayer(YapProxy* proxy, int32_t id, int32_t deltaX, int32_t deltaY);
    virtual void asyncCmdSetDNSServers(YapProxy* proxy, const char* servers);

    void initPlatformPlugin();
    virtual bool showComboBoxPopup(int id, const char* fileName);
    virtual void hideComboBoxPopup(int id);
    void clearCache();
};

class DeadlockDetectPause {
public:
    DeadlockDetectPause() { BrowserServer::instance()->pauseDeadlockDetection(); }
    ~DeadlockDetectPause() { BrowserServer::instance()->resumeDeadlockDetection(); }
};

#endif /* BROWSERSERVER_H */
