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

#ifndef BROWSERPAGE_H
#define BROWSERPAGE_H

#include <stdint.h>
#include <regex.h>
#include <glib.h>
#include <string>
#include <set>
#include <vector>
#include <weboswebpage.h>

#include <QRegion>
#include <QtWebKit/QtWebKit>
#include <QGraphicsView>
#include <QGraphicsPixmapItem>

#include "BrowserRect.h"
#include "BrowserOffscreenQt.h"
#include "BrowserOffscreenCalculations.h"

#ifdef USE_LUNA_SERVICE
#include <lunaservice.h>
#endif //USE_LUNA_SERVICE
#include <pbnjson.hpp>

#include <qbsinterface.h>

#include <semaphore.h>

#include "SSLValidationInfo.h"
#include "BrowserAdapterTypes.h"

class BrowserSyncReplyPipe;
class BrowserServer;
class YapProxy;

/**
 * Redirects only happen once the page is loaded but commands (i.e. schema's)
 * are alway's redirected.
 */
enum UrlMatchType {
    UrlMatchRedirect = 0,
    UrlMatchCommand = 1
};

enum urlOptions {
    UrlOpen ,
    PageReload ,
    None
};

class BrowserPage : public QGraphicsView, public QBsClient, public WebOSWebPageCreator, public WebOSWebPageNavigator
{
Q_OBJECT
public:

    static void setInspectorPort(int port) { inspectorPort = port > 0 ? port : 0; }

#ifdef USE_LUNA_SERVICE
    BrowserPage(BrowserServer* server, YapProxy* proxy, LSHandle* lsHandle);
#else
    BrowserPage(BrowserServer* server, YapProxy* proxy);
#endif //USE_LUNA_SERVICE

    ~BrowserPage();

    // Inherited from QGraphicsView
    virtual void paintEvent(QPaintEvent* event);

    // Inherited from QBsClient
    virtual void flushBuffer(int buffer);

    bool init(uint32_t virtualPageWidth, uint32_t virtualPageHeight, int sharedBufferKey1, int sharedBufferKey2, int sharedBufferSize);

    bool attachToBuffer(uint32_t virtualPageWidth, uint32_t virtualPageHeight,
                        int sharedBufferKey1, int sharedBufferKey2, int sharedBufferSize);

    void bufferReturned(int32_t sharedBufferKey);

    void setWindowSize(uint32_t width, uint32_t height);
    void setVirtualWindowSize(uint32_t width, uint32_t height);

    void setUserAgent(const char* pString);

    const char* getUserAgent();

    void setIdentifier(const char* id);

    const char* getIdentifier() { return m_identifier; }

    bool isBusPriviledged() { return false; } //everything running through browser server is not priviledged

    void openUrl(const char* pUrl);

    void setHTML( const char* url, const char* body );
    void setIgnoreMetaRefreshTags(bool ignore);
    void setIgnoreMetaViewport(bool ignore);
    void setNetworkInterface(const char* interfaceName);
    void setDNSServers(const char *servers);
    void disableEnhancedViewport(bool disable);

    void setShowClickedLink(bool enable);

    bool clickAt(uint32_t contentsPosX, uint32_t contentsPosY, uint32_t numClicks);
    bool holdAt(uint32_t contentsPosX, uint32_t contentsPosY);

    void keyDown(int32_t key, int32_t modifiers);

    void keyUp(int32_t key, int32_t modifiers);

    bool freeze();

    bool thaw(int sharedBufferKey1, int sharedBufferKey2, int sharedBufferSize);

    int renderToFile(const char* filename, int32_t viewX, int32_t viewY, int32_t viewW, int32_t viewH);

    void setScrollPosition(int cx, int cy, int cw, int ch);

    void getVirtualWindowSize(int& width, int& height);

    void setMinFontSize(int minFontSizePt);

    void getWindowSize(int& width, int& height);

    void resizedContents(int newWidth, int newHeight);

    void zoomedContents(double scaleFactor, int newWidth, int newHeight, int newScrollOffsetX, int newScrollOffsetY);

    void invalContents(int x, int y, int width, int height);


    void setMouseMode(enum BATypes::MouseMode mode);

    void didFinishDocumentLoad();

    void loadActive();
    void setDeadlockDetectionInterval(const int IntervalInSeconds);
    void suspendDeadlockDetection();
    void resumeDeadlockDetection();

    void urlTitleChanged(const char* uri, const char* title);

    bool canGoBackward() const;
    bool canGoForward() const;
    void clearHistory();

    void pageBackward( );

    void pageForward( );

    void pageReload( );

    void pageStop( );

    void smartZoomCalculate( uint32_t pointX, uint32_t pointY );

    void setFocus( bool bEnable );

    void getScreenSize( int& width, int& height );

    void reportError( const char* url, int code, const char* msg );

    int findString( const char* str, bool fwd );

    void selectAll();
    void cut();
    bool copy();
    void paste();
    void clearSelection();
    void setSelectionMode(bool on);

    bool dialogAlert(const char* inMsg);

    bool dialogConfirm(const char* inMsg);

    bool dialogPrompt(const char* inMsg, const char* defaultValue, std::string& result);

    bool dialogUserPassword(const char* inMsg, std::string& userName, std::string& password);

#ifdef USE_CERT_MGR
    /*
     * dialogSSLConfirm
     * 
     * return true if things went ok and user presented a response, false otherwise
     */
    bool dialogSSLConfirm(SSLValidationInfo& sslInfo);
#endif //USE_CERT_MGR

    void takeActionOnData( const char* dataType, const char* data );
    bool takeActionOnData(const char * urlString);

    void mouseEvent(int type, int contentX, int contentY, int detail);
    void gestureEvent(int type, int contentX, int contentY, double scale, double rotation, int centerX, int centerY);
    void touchEvent(int type, int32_t touchCount, int32_t modifiers, const char *touchesJson);

    // MIME streams come this way.
    void mimeHandoffUrl( const char* mimeType, const char* url );
    void mimeNotHandled( const char* mimeType, const char* url );
    bool interceptLink(const QUrl& url);
    bool displayStandaloneImages() const;
    bool shouldHandleScheme(const char* scheme) const;

    // Download callbacks from webkit
    void downloadStart( const char* /* url */ );
    void downloadProgress( const char* /* url */, unsigned long /* bytesSoFar */, unsigned long /* estimatedTotalSize */ ); 
    void downloadError( const char* /* url */, const char* /* msg */ );
    void downloadFinished( const char* /* url */, const char* /* mime type*/,  const char* /* tmp path name */ );

    void updateGlobalHistory(const char* url, bool reload);

    void downloadCancel( const char* url );

    void enableInterrogateClicks( bool enable );
    void WritePageContents( bool includeMarkup, char* outTempPath, int inMaxLen );

    void linkClicked( const char* url );

    bool isEditableAtPoint(int32_t x, int32_t y);

    bool isInteractiveAtPoint( uint32_t x, uint32_t y );
    void addUrlRedirect(const char* urlRe, UrlMatchType matchType, bool redirect, const char* userData);

    void getTextCaretBounds(int& left, int& top, int& right, int& bottom);

    void pluginSpotlightStart(int x, int y, int cx, int cy);
    void pluginSpotlightEnd();

    void hideSpellingWidget();
    void hideClipboardWidget(bool resetSelection);

    // Preferences
    void settingsPopupsEnabled(bool enable);
    void settingsJavaScriptEnabled(bool enable);

    WebOSWebPage*   createWebOSWebPage(QWebPage::WebWindowType type);
    void            closePageSoon();

    uint32_t        getPriority();
    void            setPriority(uint32_t priority);

    // For scrolling CSS objects
    void dragStart(int contentX, int contentY);
    void dragProcess(int deltaX, int deltaY);
    void dragEnd(int contentX, int contentY);

    bool saveImageAtPoint(uint32_t x, uint32_t y, QString& filepath);

    QWebHitTestResult hitTest (uint32_t x, uint32_t y);

    YapProxy*   getProxy();
    void        setProxy(YapProxy* proxy);

    bool isCardFocused() const { return m_focused; }

    virtual void viewportTagParsed(double initialScale, double minimumScale, double maximumScale, int width, int height,
             bool userScalable, bool didUseConstantsForWidth, bool didUseConstantsForHeight);

    virtual void jsObjectCleared() {}
    virtual void statusMessage(const char*) {}
    virtual void dispatchFailedLoad(const char* domain, int errorCode, const char* failingURL, const char* localizedDescription);
    virtual void setMainDocumentError(const char* domain, int errorCode, const char* failingURL, const char* localizedDescription);
    virtual void editorFocused(bool focused, const BATypes::EditorState& state);
    virtual void focused() {}
    virtual void unfocused() {}
    virtual void selectionChanged();
    virtual void makePointVisible(int x, int y);
    virtual void copiedToClipboard();
    virtual void pastedFromClipboard();
    virtual void pluginFullscreenSpotlightCreate(int handle, int rectx, int recty, int rectw, int recth);
    virtual void pluginFullscreenSpotlightRemove();
    virtual void addInteractiveWidgetRect(uintptr_t id, int x, int y, int width, int height, InteractiveRectType);
    virtual void removeInteractiveWidgetRect(uintptr_t id, InteractiveRectType);
#ifdef USE_LUNA_SERVICE
    virtual bool smartKeySearch(int requestId, const char* query);
    virtual bool smartKeyLearn(const char* word);
#endif

    void getInteractiveNodeRects(int32_t mouseX, int32_t mouseY);

    bool isEditing();

    void insertStringAtCursor(const char* text);

    // OpenSearch
    void openSearchUrl(const char* url);
    virtual void spellingWidgetVisibleRectUpdate(int x, int y, int width, int height);

    void printFrame(const char* frameName, int lpsJobId, int width, int height, int dpi, bool landscape, bool reverseOrder);

    virtual double getZoomLevel() { return m_zoomLevel; }
    int getPageX() { return m_pageX; }
    int getPageY() { return m_pageY; }
    void setZoomAndScroll(double zoom, int cx, int cy);
    void scrollLayer(int id, int deltaX, int deltaY);

    virtual void showPrintDialog();
    virtual void setCanBlitOnScroll(bool val);
    virtual void didLayout();

public Q_SLOTS:
    void doContentsSizeChanged(const QSize&);
    void doLoadStarted();
    void doLoadProgress(int);
    void doLoadFinished(bool);
    void scrolledContents(int newContentsX, int newContentsY);
    void titleChanged(const QString&);
    void urlChanged(const QUrl&);
    void updateEditorFocus();
    void restoreFrameStateRequested(QWebFrame* frame);
    void saveFrameStateRequested(QWebFrame* frame, QWebHistoryItem* item);
    void handleUnsupportedContent(QNetworkReply *);
    void doSelectionChanged();

private:

    urlOptions m_lastUrlOption;

    std::string m_lastFindString;
    BrowserServer*        m_server;
    YapProxy*             m_proxy;
    char*                 m_identifier;


    GSource*              m_paintTimer;
    QGraphicsView*        m_graphicsView;
    QGraphicsScene*       m_scene;
    QGraphicsWebView*     m_webView;
    WebOSWebPage*         m_webPage;
    std::set<void*>       m_activePopups;         ///< Set Active popup menus.
    int                   m_virtualWindowWidth;
    int                   m_virtualWindowHeight;
    int                   m_windowWidth;
    int                   m_windowHeight;
    BrowserSyncReplyPipe* m_syncReplyPipe;
    GMainLoop*            m_nestedLoop;
    BrowserPage*          m_newlyCreatedPage;
#ifdef USE_LUNA_SERVICE
    LSHandle*             m_lsHandle;
#endif //USE_LUNA_SERVICE

    BrowserOffscreenQt*   m_offscreen0;
    BrowserOffscreenQt*   m_offscreen1;
    bool                  m_ownOffscreen0;
    bool                  m_ownOffscreen1;

    uint32_t              m_priority;            ///< Used for purging on low mem notification

    bool                  m_frozen;
    int                   m_pageWidth;
    int                   m_pageHeight;
    int                   m_pageX;
    int                   m_pageY;
    double                m_zoomLevel;

    bool                  m_needsReloadOnConnect;

    QBsDriver* m_driver;
    bool m_missedPaintEvent;
    /**
     * Information about the URL's that we want to redirect.
     */
    struct UrlMatchInfo {
        regex_t urlRe;        ///< The compiled regular expression;
        bool redirect;        ///< true to redirect false to allow WebKit to navigate to url.
        std::string userData; ///< A string for use by the caller.
        std::string reStr;    ///< Saved regular expression string
        UrlMatchType type;

        UrlMatchInfo (const char* urlRe, bool redirect, const char* userData, UrlMatchType matchType);
        UrlMatchInfo(const UrlMatchInfo& rhs);
        ~UrlMatchInfo();
        bool reCompiled() const;
        private:
        UrlMatchInfo& operator=(const UrlMatchInfo& rhs) { return *this; }
    };

    std::list<UrlMatchInfo> m_urlRedirectInfo;

    unsigned int bpageId;
    std::list<int32_t> temporaryCertSerials;

    struct MetaViewport {
        bool enable;
        double initialScale;
        double maximumScale;
        double minimumScale;
        int width;
        int height;
        bool userScalable;
        bool widthEnforced;
        bool heightEnforced;
    };

    MetaViewport m_metaViewport;
    MetaViewport m_metaViewportSet;

    bool m_focused;
    int m_fingerEventCount; ///< # of times that a mouse/touch/gesture event has occurred.
    bool m_hasFocusedNode;  ///< Is there a node currently focused on the page?
    BATypes::EditorState m_lastEditorState;

#ifdef FIXME_QT
    typedef std::map<uintptr_t, ScrollableLayerItem> ScrollableLayerItemMap;
    ScrollableLayerItemMap m_scrollableLayerItems;
#endif

private:

    int32_t createIdentifier();

    void clientPointToServer(uint32_t& x, uint32_t& y);
    int mapKey(uint16_t key);
    QKeyEvent mapKeyEvent(bool pressed, uint16_t key, uint16_t modifiers);

    void handleFingerEvent();
    void initWebViewWidgetState();

    void freePtrArray(GPtrArray* array);

    void initQueuedEvents();

    bool proxyConnected();

    void invalidate();

    void updateContentScrollParamsForOffscreen();
    void calculateContentParamsForOffscreen(double zoomLevel, int contentWidth, int contentHeight, int viewportWidth, int viewportHeight);
    void calculateScrollParamsForOffscreen(int contentX, int contentY);

    void resetMetaViewport();
    void flush(int key);
#ifdef USE_LUNA_SERVICE
    static bool smartKeySearchCallback(LSHandle *sh, LSMessage *message, void *ctx);
#endif //USE_LUNA_SERVICE

    static void initKeyMap();
    static void flush(void *context, int key);
    void loadSelectionMarkers();
    void hideSelectionMarkers();

private:

    static unsigned int idGen;
    static bool keyMapInit;
#ifdef USE_LUNA_SERVICE
    static std::map<unsigned short, int> keyMap;
#else
    static std::map<int, int> keyMap;
#endif //USE_LUNA_SERVICE
    static int inspectorPort;

    bool m_ignoreMetaViewport;

    BrowserOffscreenCalculations m_offscreenCalculations;
    GSource* m_deferredContentPositionChangedTimer;

    QGraphicsPixmapItem*    m_topMarker;
    QGraphicsPixmapItem*    m_bottomMarker;
    QRect m_selectionRect;
    sem_t* m_bufferLock;
    char* m_bufferLockName;

};

#endif /* BROWSERPAGE_H */
