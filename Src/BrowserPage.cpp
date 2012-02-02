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

#include <sys/types.h>
#include <sys/shm.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <errno.h>
#include <dlfcn.h>
#include <fcntl.h>           /* For O_* constants */
#include <sys/stat.h>        /* For mode constants */
#include <limits>
#include <queue>
#include <memory>
#include <set>

#include <QtGui>
#include <QtNetwork>

#include <palmwebpage.h>
#include <palmwebview.h>
#include <qpersistentcookiejar.h>
#include <qwebevent.h>
#include <palmwebglobal.h>
#include <palmwebtypes.h>
#include <palmerrorcodes.h>
#include <cjson/json.h>
#include <pbnjson.hpp>
#include <syslog.h>

#ifdef min
// cjson defines a min macro that interferes with STL's template implementation of min
#undef min
#endif

#include "BrowserCommon.h"
#include "BrowserRect.h"
#include "BrowserSyncReplyPipe.h"
#include "BrowserServer.h"
#include "BrowserPage.h"
#include "BrowserPageManager.h"
#include "BrowserOffscreenQt.h"
#include "webosmisc.h"
#include <BufferLock.h>

#include "webosDeviceKeydefs.h"

#include "Utils.h"

#include <cert_mgr.h>

using namespace webOS;
static char* sUserAgent = 0;

// Based on iPhone viewport specifications
// http://developer.apple.com/safari/library/documentation/AppleApplications/Reference/SafariHTMLRef/Articles/MetaTags.html#//apple_ref/doc/uid/TP40008193
static const double kMetaViewportMinimumScale = 0.1;
static const double kMetaViewportMaximumScale = 2.0;
static const double kMetaViewportDefaultMinimumScale = 0.25;
static const double kMetaViewportDefaultMaximumScale = kMetaViewportMaximumScale;
static const int kMetaViewportMinWidth = 320;
static const int kMetaViewportMaxWidth = 10000;
static const int kMetaViewportMinHeight = 480;
static const int kMetaViewportMaxHeight = 10000;

static double kDoubleZeroTolerance = 0.0001;
static double kInvalidZoom = -1.0;

static const float kOffscreenWidthOverflow = 1.25f;
static const float kOffscreenSizeAsScreenSizeMultiplier = 4.0f;

static bool isPageStoppedCall = false;
const uint maxTransfer = 4095;
static char buffer[maxTransfer+1]={0};

const QString topMarkerImg("/topmarker.png");
const QString bottomMarkerImg("/bottommarker.png");
const uint selectMarkerExtraPixels = 20;

#ifdef USE_SYS_MALLOC
#define New(x)       (x*) malloc(sizeof(x))
#define Free(type,x) free(x)
#else
#define New(x)       g_slice_new(x)
#define Free(type,x) g_slice_free(type,x)
#endif

static qpa_qbs_register_client_function qpa_qbs_register_client;

template <class T>
bool ValidJsonObject(T jsonObj)
{
	return NULL != jsonObj && !is_error(jsonObj);
}


struct PaintRequest {
	BrowserPage * page;
	BrowserRect rect;
};

#if P_BACKEND == P_BACKEND_LAG
static const char* s_mapsIdentifier = "com.palm.app.maps";
#endif

unsigned int BrowserPage::idGen = 0;
bool BrowserPage::keyMapInit = false;
std::map<unsigned short, int> BrowserPage::keyMap;

int BrowserPage::inspectorPort = 0;

static inline bool PrvIsEqual(double a, double b)
{
	return (fabs(a-b) < kDoubleZeroTolerance);
}

static inline bool PrvZoomNotSet(double zoom)
{
	return PrvIsEqual(zoom, kInvalidZoom);
}

static inline QRect PrvScaledRect(int x, int y, int w, int h, double zoom)
{
	w = ::ceil((x + w) * zoom);
	h = ::ceil((y + h) * zoom);
	x = ::floor(x * zoom);
	y = ::floor(y * zoom);
	w -= x;
	h -= y;

	return QRect(x, y, w, h);
}

BrowserPage::BrowserPage(BrowserServer* server, YapProxy* proxy, LSHandle* lsHandle)
    : m_lastUrlOption(None)
    , m_server(server)
    , m_proxy(proxy)
    , m_identifier(0)
    , m_paintTimer(0)
    , m_graphicsView(0)
    , m_scene(0)
    , m_webView(0)
    , m_webPage(0)
    , m_virtualWindowWidth(0)
    , m_virtualWindowHeight(0)
    , m_windowWidth(0)
    , m_windowHeight(0)
    , m_syncReplyPipe(0)
    , m_nestedLoop(0)
    , m_newlyCreatedPage(0)
	, m_lsHandle(lsHandle)
	, m_offscreen0(0)
	, m_offscreen1(0)
	, m_ownOffscreen0(false)
	, m_ownOffscreen1(false)
    , m_priority(0)
    , m_frozen(false)
    , m_pageWidth(0)
    , m_pageHeight(0)
	, m_pageX(0)
	, m_pageY(0)
    , m_zoomLevel(1.0)
    , m_needsReloadOnConnect(false)
    , m_driver(0)
    , m_missedPaintEvent(false)
	, m_focused(true)
	, m_fingerEventCount(0)
    , m_hasFocusedNode(false)
	, m_ignoreMetaViewport(false)
	, m_deferredContentPositionChangedTimer(0)
    , m_topMarker(0)
    , m_bottomMarker(0)
    , m_selectionRect(QRect())
    , m_bufferLock(0)
    , m_bufferLockName(0)
{
	BDBG("%p", this);
	assert(proxy != NULL);
	bpageId = ++idGen;
    initKeyMap();
	resetMetaViewport();

    m_bufferLockName = createBufferLockName(proxy->postfix());
}

BrowserPage::~BrowserPage()
{
    if (m_driver)
        m_driver->releaseBuffers();

    if (qpa_qbs_register_client)
        qpa_qbs_register_client(m_graphicsView, 0);

    if (m_bufferLock) {

        sem_close(m_bufferLock);
        m_bufferLock = 0;
        sem_unlink(m_bufferLockName);

        delete []m_bufferLockName;
        m_bufferLockName = 0;
    }

	BDBG("%p", this);
    if (m_nestedLoop) {
        g_main_loop_quit(m_nestedLoop);
        g_main_loop_unref(m_nestedLoop);
        m_nestedLoop = 0;
    }
        
    BrowserPageManager::instance()->unregisterPage(this);
    
	delete m_offscreen0;
	delete m_offscreen1;
    delete m_syncReplyPipe;

    free(m_identifier);

    int result=0;
    //get rid of temporary serials
    for (std::list<int32_t>::iterator it = temporaryCertSerials.begin();it != temporaryCertSerials.end();it++) {
    	result = CertRemoveCertificate(*it);
    	if (result == CERT_OK) {
    		g_debug("BrowserServer [bpage = %u]: (dtor) removed certificate %d",bpageId,*it);
    	}
    	else {
    		g_warning("BrowserServer [bpage = %u]: (dtor) FAILED to remove certificate %d",bpageId,*it);
    	}
    }
    delete m_topMarker;
    delete m_bottomMarker;
}

void BrowserPage::paintEvent(QPaintEvent* event)
{

    if (!m_ownOffscreen0 && !m_ownOffscreen1)
        m_missedPaintEvent = true;

    QGraphicsView::paintEvent(event);
}

void BrowserPage::flushBuffer(int buffer)
{
    if (m_offscreen0 && m_offscreen1) {

        ((buffer == 0) ? m_offscreen0 : m_offscreen1)->updateParams(&m_offscreenCalculations);

        if (buffer == 0)
            m_ownOffscreen0 = false;
        else
            m_ownOffscreen1 = false;

        if (m_driver)
            m_driver->setBufferState(buffer, false);

        m_server->msgPainted(m_proxy, buffer == 0 ? m_offscreen0->key() : m_offscreen1->key());

        if (m_bufferLock) {

            if (sem_wait(m_bufferLock) == 0) {

                if (buffer == 0) {
                    m_ownOffscreen1 = true;
                    m_driver->setBufferState(1, true);
                }
                else {
                    m_ownOffscreen0 = true;
                    m_driver->setBufferState(0, true);
                }
            }
        }
    }
}

BrowserPage::UrlMatchInfo::UrlMatchInfo(const char* pRe, bool redir, const char* udata, UrlMatchType matchType) : 
	redirect(redir)
	,userData(udata)
	,reStr(pRe)
	,type(matchType)
{
	::memset(&urlRe, 0, sizeof(urlRe));
	int err = regcomp(&urlRe, pRe, REG_EXTENDED | REG_ICASE | REG_NOSUB);
	if (err) {
        syslog(LOG_ERR, "Error %d compiling re '%s'", err, pRe);
	}
}

BrowserPage::UrlMatchInfo::UrlMatchInfo(const UrlMatchInfo& rhs) :
	redirect(rhs.redirect)
	,userData(rhs.userData)
	,reStr(rhs.reStr)
	,type(rhs.type)
{
	::memset(&urlRe, 0, sizeof(urlRe));
	int err = regcomp(&urlRe, rhs.reStr.c_str(), REG_EXTENDED | REG_ICASE | REG_NOSUB);
	if (err) {
        syslog(LOG_ERR, "Error %d compiling re '%s'", err, rhs.reStr.c_str());
	}
}

BrowserPage::UrlMatchInfo::~UrlMatchInfo()
{
	if (reCompiled()) {
		regfree(&urlRe);
	}
}

bool
BrowserPage::UrlMatchInfo::reCompiled() const
{
	return urlRe.buffer != NULL;
}

void BrowserPage::addUrlRedirect(const char* urlRe, UrlMatchType matchType, bool redirect, const char* userData)
{
	m_urlRedirectInfo.push_back(UrlMatchInfo(urlRe, redirect, userData, matchType));
}

void
BrowserPage::settingsPopupsEnabled(bool enable)
{
    if (m_webPage != NULL) {
        m_webPage->settings()->setAttribute(QWebSettings::JavascriptCanOpenWindows, enable);
    }
}

void
BrowserPage::settingsJavaScriptEnabled(bool enable)
{
    if (m_webPage != NULL) {
        m_webPage->settings()->setAttribute(QWebSettings::JavascriptEnabled, enable);
    }
}

bool
BrowserPage::freeze()
{
	printf("BrowserPage::freeze: %p\n", this);
	

	if (m_frozen)
		return false;

	m_frozen = true;

    if (m_driver)
        m_driver->releaseBuffers();

	delete m_offscreen0;
	m_offscreen0 = 0;

	delete m_offscreen1;
	m_offscreen1 = 0;

	// FIXME: RR
	//   1. Disable WebKitTimer to stop background gif animation, javascripts, etc.
	//   2. Notify all plugins e.g. stop flash plugin playing audio


	return true;
}

bool
BrowserPage::thaw(int sharedBufferKey1, int sharedBufferKey2, int sharedBufferSize)
{
	printf("BrowserPage::thaw: %p\n", this);
	
	if (!m_frozen) {
		invalidate();
		return false;
	}

	m_frozen = false;

	if (sharedBufferKey1 && sharedBufferSize > 0) {
		m_offscreen0 = BrowserOffscreenQt::attach(sharedBufferKey1, sharedBufferSize);
		if (!m_offscreen0) {
            BERR("Failed to attach to shared buffer: %d with size: %d",
                 sharedBufferKey1, sharedBufferSize);
            return false;
		}
		m_ownOffscreen0 = true;
	}

	if (sharedBufferKey2 && sharedBufferSize > 0) {
		m_offscreen1 = BrowserOffscreenQt::attach(sharedBufferKey2, sharedBufferSize);
		if (!m_offscreen1) {
			BERR("Failed to attach to shared buffer: %d with size: %d",
				 sharedBufferKey2, sharedBufferSize);
			return false;
		}
		m_ownOffscreen1 = true;
	}

    if (m_driver) {

        m_graphicsView->show();

        m_driver->setBuffers(m_offscreen0->rasterBuffer(), m_offscreen0->rasterSize(), m_offscreen1->rasterBuffer(), m_offscreen1->rasterSize());
        m_driver->setBufferState(0, true);
        m_driver->setBufferState(1, true);
    }
    else
        qDebug() << "*** m_driver not set!!!";

	invalidate();

    return true;
}

void
BrowserPage::bufferReturned(int32_t sharedBufferKey)
{
	assert(m_frozen == false);
	
	if (m_offscreen0->key() == sharedBufferKey) {
//qDebug() << " $$$$$$$$$$$$$$$$$$$ Returned Buffer:" << 1;
		m_ownOffscreen0 = true;
        if (m_driver)
            m_driver->setBufferState(0, true);
	}
	else if (m_offscreen1->key() == sharedBufferKey) {
//qDebug() << " $$$$$$$$$$$$$$$$$$$ Returned Buffer:" << 2;
		m_ownOffscreen1 = true;
        if (m_driver)
            m_driver->setBufferState(1, true);
	}

    if (m_missedPaintEvent) {

        m_webView->update();
        m_missedPaintEvent = false;
    }
}

int
BrowserPage::renderToFile(const char* filename, int32_t viewX, int32_t viewY, int32_t viewW, int32_t viewH)
{
    //g_debug("renderToFile: %s, (%d, %d), w: %d, h: %d", filename, viewX, viewY, viewW, viewH);
    QImage out(viewW, viewH, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&out);
    m_graphicsView->render(&painter, QRect(viewX, viewY, viewW, viewH), QRect(viewX, viewY, viewW, viewH));
    out.save(filename);

    return 0;
}

/**
 * @brief Called when BA is ready to be paired with a BrowserPage and to 
 * display a new _target page
 *
 */
bool
BrowserPage::attachToBuffer(uint32_t virtualPageWidth, uint32_t virtualPageHeight,
							int sharedBufferKey1, int sharedBufferKey2, int sharedBufferSize)
{
	BDBG("virtualPageWidth: %d, virtualPageWidth: %d, sharedBufferKey1: %d, sharedBufferKey2: %d, sharedBufferSize: %d",
		 virtualPageWidth, virtualPageHeight, sharedBufferKey1, sharedBufferKey2, sharedBufferSize);
    
	if (sharedBufferKey1 && sharedBufferSize > 0) {
		m_offscreen0 = BrowserOffscreenQt::attach(sharedBufferKey1, sharedBufferSize);
		if (!m_offscreen0) {
            BERR("Failed to attach to shared buffer: %d with size: %d",
                 sharedBufferKey1, sharedBufferSize);
            return false;
		}
		m_ownOffscreen0 = true;
	}

	if (sharedBufferKey2 && sharedBufferSize > 0) {
		m_offscreen1 = BrowserOffscreenQt::attach(sharedBufferKey2, sharedBufferSize);
		if (!m_offscreen1) {
			BERR("Failed to attach to shared buffer: %d with size: %d",
				 sharedBufferKey2, sharedBufferSize);
			return false;
		}
		m_ownOffscreen1 = true;
	}
 
    if (m_driver) {

        m_graphicsView->show();

        m_driver->setBuffers(m_offscreen0->rasterBuffer(), m_offscreen0->rasterSize(), m_offscreen1->rasterBuffer(), m_offscreen1->rasterSize());
        m_driver->setBufferState(0, true);
        m_driver->setBufferState(1, true);
    }
    else
        qDebug() << "m_driver not set!!!";

    m_virtualWindowWidth  = virtualPageWidth;
    m_virtualWindowHeight = virtualPageHeight;

    return true;
}


bool
BrowserPage::init(uint32_t virtualPageWidth, uint32_t virtualPageHeight,
				  int sharedBufferKey1, int sharedBufferKey2, int sharedBufferSize)
{
	BDBG("virtualPageWidth: %d, virtualPageWidth: %d, sharedBufferKey1: %d, sharedBufferKey2: %d, sharedBufferSize: %d",
		 virtualPageWidth, virtualPageHeight, sharedBufferKey1, sharedBufferKey2, sharedBufferSize);

    if (!m_bufferLock && m_bufferLockName) {

        m_bufferLock = sem_open(m_bufferLockName, O_CREAT, S_IRUSR | S_IWUSR, 0);
    }

	if (sharedBufferKey1 && sharedBufferSize > 0) {
		m_offscreen0 = BrowserOffscreenQt::attach(sharedBufferKey1, sharedBufferSize);
		if (!m_offscreen0) {
            BERR("Failed to attach to shared buffer: %d with size: %d",
                 sharedBufferKey1, sharedBufferSize);
            return false;
		}
		m_ownOffscreen0 = true;
	}

	if (sharedBufferKey2 && sharedBufferSize > 0) {
		m_offscreen1 = BrowserOffscreenQt::attach(sharedBufferKey2, sharedBufferSize);
		if (!m_offscreen1) {
			BERR("Failed to attach to shared buffer: %d with size: %d",
				 sharedBufferKey2, sharedBufferSize);
			return false;
		}
		m_ownOffscreen1 = true;
	}
 
    m_virtualWindowWidth  = virtualPageWidth;
    m_virtualWindowHeight = virtualPageHeight;
	
    if (m_webPage) {
        BERR("Page already initialized");
        return false;
    }

    m_webView = new QGraphicsWebView; // new Palm::WebView(this);
    setSelectionColors(m_webView);
    m_webPage = new WebOSWebPage(m_webView);
    m_webPage->setPageCreator(this);
    m_webPage->setPageNavigator(this);
    m_webView->setPage(m_webPage);
    m_webView->setResizesToContents(true);

    m_graphicsView = this;
    QApplication::setActiveWindow(m_graphicsView);

    m_graphicsView->setFixedSize(m_virtualWindowWidth, m_virtualWindowHeight);

    m_graphicsView->setFrameStyle(QFrame::NoFrame);
    m_graphicsView->setAttribute(Qt::WA_OpaquePaintEvent, true);
    m_graphicsView->setAttribute(Qt::WA_NoSystemBackground, true);
//    m_graphicsView->setRenderHints(QPainter::Antialiasing);
    m_graphicsView->setOptimizationFlag(QGraphicsView::DontSavePainterState, true);
    m_graphicsView->setOptimizationFlag(QGraphicsView::DontAdjustForAntialiasing, true);
    m_graphicsView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_graphicsView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_scene = new QGraphicsScene(0, 0, m_virtualWindowWidth, m_virtualWindowHeight);

    m_scene->addItem(m_webView);
    m_webView->setFocus(Qt::ActiveWindowFocusReason);
    m_scene->setStickyFocus(true);
    loadSelectionMarkers();
    m_graphicsView->setScene(m_scene);

    m_graphicsView->show();

    if (m_webPage) {

        m_webPage->setNetworkAccessManager(m_server->networkAccessManager());

        connect(m_webPage, SIGNAL(loadStarted()), this, SLOT(doLoadStarted()));
        connect(m_webPage, SIGNAL(loadProgress(int)), this, SLOT(doLoadProgress(int)));
        connect(m_webPage, SIGNAL(loadFinished(bool)), this, SLOT(doLoadFinished(bool)));
        //connect(m_webPage, SIGNAL(scrollRequested(int, int, const QRect&)), SLOT(scrolledContents(int, int)));
        connect(m_webPage->mainFrame(), SIGNAL(contentsSizeChanged(const QSize&)), this, SLOT(doContentsSizeChanged(const QSize&)));
        connect(m_webPage->mainFrame(), SIGNAL(titleChanged(const QString&)), this, SLOT(titleChanged(const QString&)));
        connect(m_webPage->mainFrame(), SIGNAL(urlChanged(const QUrl&)), this, SLOT(urlChanged(const QUrl&)));
        connect(m_webPage, SIGNAL(microFocusChanged()), this, SLOT(updateEditorFocus()));
        connect(m_webPage, SIGNAL(restoreFrameStateRequested(QWebFrame*)), this, SLOT(restoreFrameStateRequested(QWebFrame*)));
        connect(m_webPage, SIGNAL(saveFrameStateRequested(QWebFrame*, QWebHistoryItem*)), this, SLOT(saveFrameStateRequested(QWebFrame*, QWebHistoryItem*)));
        connect(m_webPage, SIGNAL(unsupportedContent(QNetworkReply*)), this, SLOT(handleUnsupportedContent(QNetworkReply*)));
        connect(m_webPage, SIGNAL(selectionChanged()), this, SLOT(doSelectionChanged()));
        m_webPage->setForwardUnsupportedContent(true);

        if (inspectorPort)
            m_webPage->setProperty("_q_webInspectorServerPort", inspectorPort);
    }

	BrowserPageManager::instance()->registerPage(this);

    if (!qpa_qbs_register_client) {

       void *handle = dlopen("/usr/plugins/platforms/libqbsplugin.so", RTLD_LAZY);

       if (handle)
           qpa_qbs_register_client = (qpa_qbs_register_client_function)dlsym(handle, "qpa_qbs_register_client");
       else
          qDebug() << "### /usr/plugins/platforms/libqbsplugin.so NOT FOUND!!!";
    }
    
    if (qpa_qbs_register_client)
        m_driver = qpa_qbs_register_client(m_graphicsView, this);

    if (m_driver && m_offscreen0 && m_offscreen1) {

        m_driver->setBuffers(m_offscreen0->rasterBuffer(), m_offscreen0->rasterSize(), m_offscreen1->rasterBuffer(), m_offscreen1->rasterSize());
        m_driver->setBufferState(0, true);
        m_driver->setBufferState(1, true);
    }
    else
        qDebug() << "### m_driver not set!!!";

    return true;
}    

void BrowserPage::setWindowSize(uint32_t width, uint32_t height)
{
    BDBG("Window size: %dx%d", width, height);

    if (m_metaViewport.enable) {
    	if (m_metaViewport.widthEnforced)
    		width = m_metaViewport.width;
    	if (m_metaViewport.heightEnforced)
    		height = m_metaViewport.height;
    }

	if (m_windowWidth == (int) width && m_windowHeight == (int) height)
		return;

	m_windowWidth = width;
	m_windowHeight = height;

    m_webView->page()->setPreferredContentsSize(QSize(width, height));

	updateContentScrollParamsForOffscreen();
	
	// repaint
	invalidate();
}

void BrowserPage::setVirtualWindowSize(uint32_t width, uint32_t height)
{
    BDBG("Virtual window size: %dx%d", width, height);

    if (m_metaViewport.enable) {
		if (m_metaViewport.widthEnforced)
			width = m_metaViewport.width;
		if (m_metaViewport.heightEnforced)
			height = m_metaViewport.height;
	}

	if (m_virtualWindowWidth == (int) width && m_virtualWindowHeight == (int) height)
		return;

	m_virtualWindowWidth = width;
	m_virtualWindowHeight = height;

	/* repaint */
	invalidate();
}

void
BrowserPage::setUserAgent(const char* pString)
{
    if (!pString)
        return;
        
    if (sUserAgent) {
        ::free(sUserAgent);
        sUserAgent = 0;
    }

    sUserAgent = ::strdup(pString);
    BDBG("Set User Agent: %s", sUserAgent);
}

const char*
BrowserPage::getUserAgent()
{
	return sUserAgent;
}

void
BrowserPage::setIdentifier(const char* id)
{
    free(m_identifier);
    m_identifier = 0;

    m_identifier = strdup(id);
}

void
BrowserPage::openUrl(const char* pUrl)
{
	BDBG("BrowserPage::openUrl: '%s'", pUrl);
    
    if (!m_webPage) {
        BERR("No page created");
        return;
    }


    	snprintf (buffer, maxTransfer , "\n URL = %s \n" , pUrl);



    m_lastUrlOption = UrlOpen;

    QUrl url = QUrl::fromUserInput(QString::fromUtf8(pUrl));

    m_webPage->mainFrame()->load(url);
}

void 
BrowserPage::setHTML( const char* url, const char* body )
{
    BDBG("setHTML");
    
    if (!m_webPage) {
        BERR("No page created");
        return;
    }

    	snprintf (buffer, maxTransfer , "\n URL = %s \n BODY = %s \n" , url , body);

    m_webPage->mainFrame()->setHtml(body, QUrl(url));

}

void
BrowserPage::setShowClickedLink(bool enable)
{
    if (!m_webPage) {
        BERR("No page created");
        return;
    }
    
#ifdef FIXME_QT
    m_webView->showClickedLink(enable);
#endif // FIXME_QT
}

void 
BrowserPage::pageBackward( )
{
	BDBG("pageBack" );
    if (!m_webPage) {
        BERR("No page created");
        return;
    }

    m_webPage->triggerAction(QWebPage::Back);
}

void 
BrowserPage::pageForward( )
{
	BDBG("pageForward" );
    if (!m_webPage) {
        BERR("No page created");
        return;
    }

    m_webPage->triggerAction(QWebPage::Forward);
}

void 
BrowserPage::pageReload( )
{
	BDBG("pageReload" );
    if (!m_webPage) {
        BERR("No page created");
        return;
    }


    m_lastUrlOption = PageReload;

    m_webPage->triggerAction(QWebPage::Reload);
}

void 
BrowserPage::pageStop( )
{
	BDBG("pageStop");
    if (!m_webPage) {
        BERR("No page created");
        return;
    }

    isPageStoppedCall = true;

    m_webPage->triggerAction(QWebPage::Stop);
}

void BrowserPage::smartZoomCalculate( uint32_t pointX, uint32_t pointY  )
{
	BDBG("smartZoomCalculate" );
    
    if (!m_webPage) {
        BDBG("%s: No page created.", __FUNCTION__);
        return;
    }

#ifdef FIXME_QT
    Palm::WebRect rect;
    int fullscreenSpotlight=0;
    
    if (!m_webView->smartZoomCalculate(pointX, pointY, rect, &fullscreenSpotlight)) {
        // set to empty rect
        rect.left = 0;
        rect.top = 0;
        rect.right = 0;
        rect.bottom = 0;
    }
    
    // g_message("%s: SmartZoomCalculateResponseSimple: %d : %d, %d : %d\n",
    //           __FUNCTION__, rect.left, rect.right, rect.top, rect.bottom);
    
    m_server->msgSmartZoomCalculateResponseSimple(m_proxy, pointX, pointY, rect.left, rect.top, rect.right, rect.bottom, fullscreenSpotlight);
#endif // FIXME_QT
}


void BrowserPage::editorFocused(bool focused, const PalmIME::EditorState& state)
{
    m_hasFocusedNode = focused;
    m_lastEditorState = state;
    if (0 == m_fingerEventCount)
        return; // Only focus an editor after the first mouse/touch event.
	
    if (m_server) {
		m_server->msgEditorFocused(m_proxy, focused, state.type, state.actions);

		int left(0), top(0), right(0), bottom(0);
        getTextCaretBounds(left, top, right, bottom);
		m_server->msgGetTextCaretBoundsResponse(m_proxy, 0, left, top, right, bottom);
    }
}


void BrowserPage::makePointVisible(int x, int y)
{
    if (m_server) {
        m_server->msgMakePointVisible(m_proxy, x, y);
    }
}

void
BrowserPage::setFocus( bool bEnable )
{
	if (!m_webPage) {
        BERR("No page created");
        return;
    }

	if (m_focused == bEnable)
		return;

#ifdef FIXME_QT
	m_webView->setViewIsActive(bEnable);
#endif // FIXME_QT
	m_focused = bEnable;

    if (m_focused) {
        QApplication::setActiveWindow(m_graphicsView);
        invalidate();
    }
}

/**
 * Initialize the WebView widget's focus state.
 */
void
BrowserPage::initWebViewWidgetState()
{
    if (m_hasFocusedNode) {
        // Set the finger event count to > 0 so that editorFocused will correctly blur
        m_fingerEventCount = 1;
        editorFocused(false, PalmIME::FieldType_Text);
    }
    m_fingerEventCount = 0;

    // Make the view inactive so that the text insertion point isn't blinking (until a
    // tap occurs to focus the widget).
    m_webView->clearFocus();
}

/**
 * Do work on each finger (mouse/tap/click) event.
 *
 * Handle a mouse/touch/gesture (AKA finger) event and make the WebView widget 
 * focused if it isn't already focused.
 */
void
BrowserPage::handleFingerEvent()
{
    if (0 == m_fingerEventCount) {
        // Now that a mouse/touch/gesture (AKA finger) event has occurred let's focus
        // the WebView so that it will have blinking text insertion points, etc.
        m_webView->setFocus(Qt::ActiveWindowFocusReason);
    }

    m_fingerEventCount++;
}

bool
BrowserPage::clickAt(uint32_t contentsPosX, uint32_t contentsPosY, uint32_t numClicks)
{
    if (!m_webPage) {
        BERR("No page created");
        return false;
    }

    bool sendIgnoredEditorFocusedMsg = (m_fingerEventCount == 0 && m_hasFocusedNode);

    handleFingerEvent();

    // FIXME: we send only single clicks currently
	
	BDBG("Click at: %dx%d", contentsPosX, contentsPosY);
	clientPointToServer(contentsPosX, contentsPosY);
	BDBG("Click mapped to %dx%d", contentsPosX, contentsPosY);

	// We fake a single click by sending one press and then a quick release
    QMouseEvent down(QEvent::MouseButtonPress, QPoint(contentsPosX, contentsPosY), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QMouseEvent up(QEvent::MouseButtonRelease, QPoint(contentsPosX, contentsPosY), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);

    bool handledDown = QCoreApplication::sendEvent(m_graphicsView->viewport(), &down);
    bool handledUp = QCoreApplication::sendEvent(m_graphicsView->viewport(), &up);

    //FIXME: For accuracy, Use Region based check instead of bounding rect
    if(!m_selectionRect.contains(contentsPosX,contentsPosY)
        && !m_topMarker->boundingRect().contains(contentsPosX,contentsPosY)
        && !m_bottomMarker->boundingRect().contains(contentsPosX,contentsPosY)
      )
        hideSelectionMarkers();
	
	BDBG("HandledDown:%s, HandledUp:%s", handledDown ? "TRUE":"false", handledUp ? "TRUE":"false");

    if (sendIgnoredEditorFocusedMsg)
        editorFocused(m_hasFocusedNode, m_lastEditorState);
    else
        updateEditorFocus();
	
	return handledDown || handledUp;
}

bool
BrowserPage::holdAt(uint32_t contentsPosX, uint32_t contentsPosY)
{
    if (!m_webPage) {
        BERR("No page created");
        return false;
    }

    QWebHitTestResult hitResult = hitTest(contentsPosX, contentsPosY);
    BDBG("Hold at: %dx%d", contentsPosX, contentsPosY);
    clientPointToServer(contentsPosX, contentsPosY);
    BDBG("Hold mapped to %dx%d", contentsPosX, contentsPosY);

    if(hitResult.isTextNode() || hitResult.isContentEditable()) {
        QMouseEvent doubleClickEvent(QEvent::MouseButtonDblClick, QPoint(contentsPosX, contentsPosY), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        bool handledHold = QCoreApplication::sendEvent(m_graphicsView->viewport(), &doubleClickEvent);
        return handledHold;
    }
    return false;
}
void
BrowserPage::keyDown(uint16_t key, uint16_t modifiers)
{
    BDBG("Key Down: %d (0x%02x, %c)", key, key, key);

	// Check for a clipboard operation request. This comes from palmwebview.cpp in the
	// form of a key up/down pair.
	if( modifiers & 0x20 ) {
		switch( key ) {
		case Key_c:
		case Key_x:
		case Key_v:
		case Key_a:
			return;
		} 
	}
	
    QKeyEvent event = mapKeyEvent(true, key, modifiers);
    QCoreApplication::sendEvent(m_graphicsView->viewport(), &event);
}

void
BrowserPage::keyUp(uint16_t key, uint16_t modifiers)
{
    BDBG("Key Up: %d (0x%02x, %c)", key, key, key);
	
	if( modifiers & 0x20 ) {
		switch( key ) {
		case Key_c:
		case Key_x:
		case Key_v:
		case Key_a:
			return;
		} 
	}
	
    QKeyEvent event = mapKeyEvent(false, key, modifiers);
    QCoreApplication::sendEvent(m_graphicsView->viewport(), &event);
}

// used to notify plugins of current viewport
void BrowserPage::setScrollPosition(int cx, int cy, int cw, int ch)
{
	if (!m_webView)
		return;

	// FIXME: RR. should check if zoomLevel is set
	
	cx = cx / m_zoomLevel;
	cy = cy / m_zoomLevel;
	
	cx = MAX(0, cx);
	cy = MAX(0, cy);
	
	m_pageX = cx;
	m_pageY = cy;

	updateContentScrollParamsForOffscreen();
}

void
BrowserPage::getVirtualWindowSize(int& width, int& height)
{
	if (m_metaViewport.enable) {
		width = m_metaViewport.width;
		height = MIN(m_metaViewport.height, m_virtualWindowHeight);
		return;
	}
		
	width = m_windowWidth;
	height = m_windowHeight;
}

void
BrowserPage::getScreenSize( int& width, int& height )
{
	// this returns the exact device pixel count for the window.
	// We're using this separate method from getWindowSize() since the latter
	// is tied up with the tile logic.
	width = m_windowWidth;
	height = m_windowHeight;
	//BDBG("BrowserPage::screenSize --> %d, %d", width, height );
}

void 
BrowserPage::enableInterrogateClicks( bool enable )
{
#ifdef FIXME_QT
	m_webView->setInterrogateClicks(enable);
#endif // FIXME_QT
}

void 
BrowserPage::WritePageContents( bool includeMarkup, char* outTempPath, int inMaxLen )
{
	
/*	FIXME: RR
	char* contents=0;
	
	outTempPath[0]=0;
	webkit_page_get_contents( m_webPage, includeMarkup ? 1 : 0, &contents );
	if( contents )
	{
		// TODO : replace this with the "real" tep path
		char temp_path[ 64 ] = "/tmp/webXXXXXXXXXX";
		if( -1 != mkstemp( temp_path ) )
		{
			FILE* f = fopen( temp_path, "w" );
			if(f)
			{
				fwrite( contents, strlen(contents), 1, f ); 
				fclose(f);
				strcpy( outTempPath, temp_path );
			}
		}
		delete[] contents;
	}
*/
}

void 
BrowserPage::linkClicked( const char* url )
{
    m_server->msgLinkClicked(m_proxy, url);	
}

// Maps from coordinate system sent by client, to the one used internally.
// (just subtracts the top left of the page's content rect)
void BrowserPage::clientPointToServer(uint32_t& x, uint32_t& y)
{
    x = (x * m_zoomLevel) - m_offscreenCalculations.renderX;
    y = (y * m_zoomLevel) - m_offscreenCalculations.renderY;
}

int
BrowserPage::mapKey(uint16_t key) {

    const std::map<unsigned short, int>::const_iterator it = keyMap.find (key);

    if (it != keyMap.end())
        return (*it).second;

    if (key >= Key_Space && key < 0xE000)
        return (int)key;

    return 0;
}

QKeyEvent
BrowserPage::mapKeyEvent(bool pressed, uint16_t key, uint16_t modifiers) {

    static const uint16_t ShiftModifier = 0x80;
    static const uint16_t ControlModifier = 0x40;
    static const uint16_t MetaModifier = 0x20;

    int mappedKey = mapKey(key);

    Qt::KeyboardModifiers mappedModifiers = Qt::NoModifier;
    if (modifiers & ShiftModifier) {
        mappedModifiers |= Qt::ShiftModifier;
    }
    else if ((key >= Key_a) && (key <= Key_z))
        mappedKey -= Key_a - Key_A;

    if (modifiers & ControlModifier)
        mappedModifiers |= Qt::ControlModifier;
    if (modifiers & MetaModifier)
        mappedModifiers |= Qt::MetaModifier;

    QString text;

    if (key < 0xE000)
        text += QChar(key);

    QKeyEvent event(pressed ? QEvent::KeyPress : QEvent::KeyRelease, mappedKey, mappedModifiers, text);

    return event;
}


bool
BrowserPage::isEditableAtPoint(int32_t x, int32_t y)
{
    return hitTest(x, y).isContentEditable();
}

bool 
BrowserPage::isInteractiveAtPoint( uint32_t x, uint32_t y )
{
    QWebHitTestResult hitResult = hitTest(x, y);

    if (hitResult.isNull())
        return false;

    if (hitResult.isContentEditable())
        return true;

    if (!hitResult.linkUrl().isEmpty())
        return true;

    QString tagName = hitResult.element().tagName().toLower();
    if (tagName == QLatin1String("input") || tagName == QLatin1String("select"))
        return true;

    return false;
}

void BrowserPage::getTextCaretBounds(int& left, int& top, int& right, int& bottom)
{
    if (m_webView) {
        QRect boundingRect;
        m_webView->page()->getTextCaretPos(boundingRect);
        left = boundingRect.left();
        top = boundingRect.top();
        right = boundingRect.right();
        bottom = boundingRect.bottom();
    }
    else {
        left = top = right = bottom = 0;
    }
}

void BrowserPage::dragStart(int contentX, int contentY)
{
}

void BrowserPage::dragProcess(int deltaX, int deltaY)
{
}
void BrowserPage::dragEnd(int contentX, int contentY)
{
}

void
BrowserPage::reportError( const char* url, int code, const char* msg )
{
    m_server->msgReportError(m_proxy, url, code, msg);
}

int
BrowserPage::findString( const char* inStr, bool fwd )
{
#ifdef FIXME_QT
	m_webView->getTextCaretPos(left, top, right, bottom);
	if( !m_webPage )
		return false;
	
	std::string newStr( inStr );
	bool start_in_selection;
	size_t len_to_check = std::min( newStr.size(), m_lastFindString.size() );
	
	if( !strncmp( inStr, m_lastFindString.c_str(), len_to_check) )
		start_in_selection = true;
	else
		start_in_selection = false;
	
	if( m_lastFindString == newStr )
		start_in_selection = false;
	
	//printf( "m_lastFindString=%d  last=%s  current=%s len_to_check=%d fwd=%d\n", start_in_selection, m_lastFindString.c_str(), inStr, len_to_check, fwd );
	
	int numHits = m_webView->findString(inStr, 
			/* search fwd= */ fwd, 
			/* case sensitive = */ false, 
			/* wrap= */ true, 
			/* start with sel= */ start_in_selection );
	
	m_lastFindString = inStr;
	
	if( !numHits )
		m_webView->clearSelection();
		
	return numHits;
#else
    return 0;
#endif // FIXME_QT
}

void
BrowserPage::setSelectionMode(bool on)
{
    if (!m_webPage) {
        return;
    }
//    printf("%s: BS, changing selection mode to %s\n", __FUNCTION__, 
//            (on) ? "ON" : "OFF");
    
#ifdef FIXME_QT
    m_webView->setSelectionMode(on);
#endif // FIXME_QT
}

void
BrowserPage::selectAll()
{
    if( !m_webPage )
        return;
    
#ifdef FIXME_QT
    m_webView->selectAll();
#endif // FIXME_QT
}

void
BrowserPage::cut()
{
    if( !m_webPage )
        return;
    
#ifdef FIXME_QT
    m_webView->cut();
#endif // FIXME_QT
}

bool
BrowserPage::copy()
{
    if( !m_webPage )
        return false;
    
#ifdef FIXME_QT
    return m_webView->copy();
#else
    return false;
#endif // FIXME_QT
}


void
BrowserPage::paste()
{
    if( !m_webPage )
        return;
    
#ifdef FIXME_QT
    m_webView->paste();
#endif // FIXME_QT
}

void 
BrowserPage::clearSelection( )
{
	if( !m_webPage )
		return;
#ifdef FIXME_QT
	m_webView->clearSelection();
	m_lastFindString.clear();
#endif // FIXME_QT
}

void 
BrowserPage::setMinFontSize(int minFontSizePt)
{
	if( !m_webPage )
		return;
#ifdef FIXME_QT
	m_webView->setMinFontSize(minFontSizePt);
#endif // FIXME_QT
}

void
BrowserPage::getWindowSize(int& width, int& height)
{
	if (m_metaViewport.enable) {
		width = m_metaViewport.width;
		height = MIN(m_metaViewport.height, m_virtualWindowHeight);
		return;
	}			 
	
//	width = m_virtualWindowWidth;
//	height = m_virtualWindowHeight;
	width = m_windowWidth;
	height = m_windowHeight;
}

void
BrowserPage::resizedContents(int newWidth, int newHeight)
{
    if (!m_webPage) {
        BERR("No page created for this BrowserPage yet");
        return;
    }

    zoomedContents(1.0, newWidth, newHeight, 0, 0);
}

void
BrowserPage::zoomedContents(double scaleFactor, int newWidth, int newHeight, int newScrollOffsetX, int newScrollOffsetY)
{
    if (!m_webPage) {
        BERR("No page created for this BrowserPage yet");
        return;
    }

	if ((newWidth == m_pageWidth && newWidth == 0) ||
		(newHeight == m_pageHeight && newHeight == 0))
		return;

    BDBG("Zoomed contents: %dx%d scrolloffset=%d,%d sf=%g", newWidth, newHeight, 
		 newScrollOffsetX, newScrollOffsetY, scaleFactor);

	if (newWidth == 0 && newHeight == 0) {
		// start of a new page
        initWebViewWidgetState();
		m_scrollableLayerItems.clear();
		resetMetaViewport();
		m_zoomLevel = kInvalidZoom;
		m_pageX = 0;
		m_pageY = 0;
	}
    else if(m_metaViewport.enable) {
        resetMetaViewport();
    }
	else {

		newWidth = newWidth / scaleFactor;
		newHeight = newHeight / scaleFactor;
	
		if (PrvZoomNotSet(m_zoomLevel) && m_windowWidth)
			m_zoomLevel = newWidth / m_windowWidth;
	}

	m_pageWidth = newWidth;
	m_pageHeight = newHeight;

	m_server->msgContentsSizeChanged(m_proxy, newWidth, newHeight);

	updateContentScrollParamsForOffscreen();
}

void
BrowserPage::doLoadStarted()
{
    initWebViewWidgetState();

    if (!m_webPage) {
        // This should never happen
        BERR("No page created");
        return;
    }

	BDBG("loadStarted");


    m_server->msgLoadStarted(m_proxy);
}

void
BrowserPage::doLoadFinished(bool ok)
{
    if (!m_webPage) {
        // This should never happen
        BERR("No page created");
        return;
    }

	BDBG("loadStopped");
	
    m_server->msgLoadStopped(m_proxy);

    if(!ok)
    {
        const QByteArray domainString[]={"QtNetwork","Http","WebKit"};
        const QWebPage::ErrorPageExtensionOption* errorInfo = m_webPage->getErrorInfo();
        if (errorInfo)
            setMainDocumentError(domainString[errorInfo->domain].constData(), errorInfo->error, errorInfo->url.toString().toLatin1().constData(), errorInfo->errorString.toLatin1().constData());
    }

	if ( isPageStoppedCall )
	isPageStoppedCall = false;


	switch(m_lastUrlOption) {
	default: break;
	}
	m_lastUrlOption = None;
}

void
BrowserPage::scrolledContents(int newContentsX, int newContentsY)
{
//    m_server->msgScrolledTo(m_proxy, newContentsX, newContentsY);
}

void
BrowserPage::titleChanged(const QString& title) {

    if (!m_webView)
        return;

    QUrl url = m_webView->page()->mainFrame()->url();

    m_server->msgTitleAndUrlChanged(m_proxy, title.toUtf8().constData(), url.toEncoded().constData(), canGoBackward(), canGoForward());
}

void
BrowserPage::urlChanged(const QUrl& url) {

    if (!m_webView)
        return;

    QString title = m_webView->page()->mainFrame()->title();

    m_server->msgTitleAndUrlChanged(m_proxy, title.toUtf8().constData(), url.toEncoded().constData(), canGoBackward(), canGoForward());
}

void
BrowserPage::updateEditorFocus() {
    bool active = m_webView->flags() & QGraphicsItem::ItemAcceptsInputMethod;
    PalmIME::EditorState editorState(PalmIME::FieldType_Text);
    if (active) {
        Qt::InputMethodHints hints = m_webView->inputMethodHints();
        if (hints & Qt::ImhEmailCharactersOnly) {
            editorState.type = PalmIME::FieldType_Email;
        } else if (hints & Qt::ImhUrlCharactersOnly) {
            editorState.type = PalmIME::FieldType_URL;
        } else if (hints & Qt::ImhDialableCharactersOnly) {
            editorState.type = PalmIME::FieldType_Phone;
        } else if (hints & (Qt::ImhPreferNumbers | Qt::ImhDigitsOnly | Qt::ImhFormattedNumbersOnly)) {
            editorState.type = PalmIME::FieldType_Number;
        }
    }

    editorFocused(active, editorState);
}

void BrowserPage::restoreFrameStateRequested(QWebFrame* frame)
{
    if (frame == m_webPage->mainFrame()) {
        //qDebug() << "RESTORE:" << frame->page()->history()->currentItem().url().toString() << m_zoomLevel;
    }
}

void BrowserPage::saveFrameStateRequested(QWebFrame* frame, QWebHistoryItem* item)
{
    if (frame == m_webPage->mainFrame()) {

        //qDebug() << "SAVE:" << item->url().toString() << m_zoomLevel;

        m_server->msgContentsSizeChanged(m_proxy, 0, 0);
    }
}

void
BrowserPage::handleUnsupportedContent(QNetworkReply *reply)
{
    if(!reply)
        return;

    QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    QString url = reply->url().toString();

    if (!contentType.isEmpty() && !url.isEmpty()) {

        mimeNotHandled(qPrintable(contentType), qPrintable(url));
        return;
    }
}

void
BrowserPage::updateGlobalHistory(const char* url, bool reload)
{
    if (!m_webPage) {
        // This should never happen
        BERR("No page created");
        return;
    }
    
    m_server->msgUpdateGlobalHistory(m_proxy, url, reload);
}

void
BrowserPage::didFinishDocumentLoad()
{
    if (!m_webPage) {
        // This should never happen
        BERR("No page created");
        return;
    }

    m_server->msgDidFinishDocumentLoad(m_proxy);
}

void
BrowserPage::doLoadProgress(int progress)
{
    if (!m_webPage) {
        // This should never happen
        BERR("No page created");
        return;
    }

    m_server->msgLoadProgress(m_proxy, progress);
}

void
BrowserPage::loadActive()
{
    m_server->incrementDeadlockCounter();
}

void
BrowserPage::setDeadlockDetectionInterval(const int IntervalInSeconds)
{
    m_server->setDeadlockDetectionInterval(IntervalInSeconds);
}

void
BrowserPage::suspendDeadlockDetection()
{
    m_server->pauseDeadlockDetection();
}

void
BrowserPage::resumeDeadlockDetection()
{
    m_server->resumeDeadlockDetection();
}

void 
BrowserPage::urlTitleChanged(const char* pUrl, const char* pTitle)
{
    if (!m_webPage) {
        // This should never happen
        BERR("No page created");
        return;
    }

    m_server->msgTitleAndUrlChanged(m_proxy, pTitle, pUrl, canGoBackward(), canGoForward());
}

void BrowserPage::setMouseMode(Palm::MouseMode mode)
{
#ifdef FIXME_QT
	if (m_webView) {
		return m_webView->setMouseMode(mode);
	}
#endif // FIXME_QT
}

bool BrowserPage::canGoBackward() const
{
	if (m_webPage) {
		return m_webPage->history()->canGoBack();
	}
	else {
        BERR("No page created");
		return false;
	}
}

bool BrowserPage::canGoForward() const
{
	if (m_webPage) {
		return m_webPage->history()->canGoForward();
	}
	else {
        BERR("No page created");
		return false;
	}
}

void BrowserPage::clearHistory()
{
	if (m_webPage) {
		m_webPage->history()->clear();
	}
	else {
        BERR("No page created");
	}
}

void BrowserPage::freePtrArray(GPtrArray* array)
{
	for (unsigned int i = 0; i < array->len; i++) {
        ::free( g_ptr_array_index(array, i) );
    }

    g_ptr_array_free(array, TRUE);
}

// Workaround for DFISH-5455
bool BrowserPage::proxyConnected()
{
	if (!m_proxy->connected()) {
	    g_warning("proxy not connected, stopping page load");
	    pageStop();
	    m_needsReloadOnConnect = true;
		return false;
	}
	return true;
}

bool
BrowserPage::dialogAlert(const char* inMsg)
{
    if (!proxyConnected())
		return false;

    DeadlockDetectPause pauser;

	if (!m_syncReplyPipe)
        m_syncReplyPipe = new BrowserSyncReplyPipe(this);

    m_server->msgDialogAlert(m_proxy, m_syncReplyPipe->pipePath(), inMsg);

    GPtrArray* replyArray;
    if (!m_syncReplyPipe->getReply(&replyArray, m_proxy->commandSocketFd()))
        return false;

	freePtrArray(replyArray);

    return true;
}

bool
BrowserPage::dialogConfirm(const char* inMsg)
{
    if (!proxyConnected())
		return false;

    DeadlockDetectPause pauser;

	if (!m_syncReplyPipe)
        m_syncReplyPipe = new BrowserSyncReplyPipe(this);

    m_server->msgDialogConfirm(m_proxy, m_syncReplyPipe->pipePath(), inMsg);    

    GPtrArray* replyArray;
    if (!m_syncReplyPipe->getReply(&replyArray, m_proxy->commandSocketFd()))
        return false;

    bool ok = ::atoi((char*)g_ptr_array_index(replyArray, 0));
    
    freePtrArray(replyArray);

    return ok;
}

bool
BrowserPage::dialogPrompt(const char* inMsg, const char* defaultValue, std::string& result)
{
    if (!proxyConnected())
		return false;

    DeadlockDetectPause pauser;
	
	if (!m_syncReplyPipe)
        m_syncReplyPipe = new BrowserSyncReplyPipe(this);

    m_server->msgDialogPrompt(m_proxy, m_syncReplyPipe->pipePath(), inMsg, defaultValue);    

    GPtrArray* replyArray;
    if (!m_syncReplyPipe->getReply(&replyArray, m_proxy->commandSocketFd()))
        return false;

    bool ok = ::atoi((char*)g_ptr_array_index(replyArray, 0));
    if (ok) {
        char* val = (char*)g_ptr_array_index(replyArray, 1);
        if (val)
            result = val;
        else
			result = "";
    }
    
    freePtrArray(replyArray);

    return ok;
}

bool
BrowserPage::dialogUserPassword(const char* inMsg, std::string& userName, std::string& password)
{
	if (!proxyConnected())
		return false;

	DeadlockDetectPause pauser;

	if (!m_syncReplyPipe)
        m_syncReplyPipe = new BrowserSyncReplyPipe(this);

	userName.clear();
	password.clear();

    m_server->msgDialogUserPassword(m_proxy, m_syncReplyPipe->pipePath(), inMsg);

    GPtrArray* replyArray(NULL);
    if (!m_syncReplyPipe->getReply(&replyArray, m_proxy->commandSocketFd()) || replyArray == NULL)
        return false;

    bool ok = ::atoi(static_cast<const char*>(g_ptr_array_index(replyArray, 0))) == 0 ? false : true;
    if (ok) {
		const char* val(NULL);

		if (replyArray->len >= 2) {
			val = static_cast<const char*>(g_ptr_array_index(replyArray, 1));
			if (replyArray)
				userName = val;
		}
		else {
			BERR("Username not provided");
			ok = false;
		}

		if (replyArray->len >= 3) {
			val = static_cast<const char*>(g_ptr_array_index(replyArray, 2));
			if (val)
				password = val;
		}
		else {
			BERR("Password not provided");
			ok = false;
		}
    }
    
    freePtrArray(replyArray);

    return ok;
}

/*
 * POTENTIAL ISSUES:
 * 
 * If an instance of BrowserPage was to temporarily accept some certs (installing them to the store), but then somehow exit
 * *WITHOUT* getting destructed (BrowserServer crash is the most likely cause), those certs effectively become permanently
 * accepted.
 * 
 * TODO
 * Supposedly CertAddTrustedCert() marks the certs as being exceptionally trusted. Therefore, it should be able to run
 * a sweep through the store when BrowserServer starts up and expunge all such certs
 * 
 */
bool
BrowserPage::dialogSSLConfirm(Palm::SSLValidationInfo& sslInfo)
{
	if (!proxyConnected())
		return false;
	
    DeadlockDetectPause pauser;

    /*
	 * Then figure out if this cert has been installed. If so, then no need to keep going
	 * because it's been accepted already
	 */

	g_debug("BrowserServer [bpage = %u]: (ssl) Page flagged for cert verification...",bpageId);
	
	int inStoreCertSerial = 0;
	X509 * inStoreCert = findSSLCertInLocalStore(sslInfo.getCertFile().c_str(),inStoreCertSerial);

	if (inStoreCert != NULL) {
		g_debug("BrowserServer [bpage = %u]: (ssl) cert found in store (serial %u). Returning automatic accept",bpageId,inStoreCertSerial);
		//found it!...just reply as accept
		sslInfo.setAcceptDecision(1);
		X509_free(inStoreCert);
        unlink(sslInfo.getCertFile().c_str());
		return true;
	}
	
	g_debug("BrowserServer [bpage = %u]: (ssl) cert not found in store. Asking user via dialog...",bpageId);
	//else, need to ask the user
	
	if (!m_syncReplyPipe)
		m_syncReplyPipe = new BrowserSyncReplyPipe(this);

	m_server->msgDialogSSLConfirm(
		m_proxy, m_syncReplyPipe->pipePath(),
		sslInfo.getHostName().c_str(),
		sslInfo.getValidationFailureReasonCode(),
		sslInfo.getCertFile().c_str());

	g_debug("BrowserServer [bpage = %u]: (ssl) sent up the dialog",bpageId);
	
	GPtrArray* replyArray;
	if (!m_syncReplyPipe->getReply(&replyArray, m_proxy->commandSocketFd())) {
		g_warning("BrowserServer [bpage = %u]: (ssl) returned from dialog - reply error",bpageId);
        unlink(sslInfo.getCertFile().c_str());
		return false;
	}

	int dialogResponse = ::atoi((char*)g_ptr_array_index(replyArray, 0));
	g_debug("BrowserServer [bpage = %u]: (ssl) return from dialog, reply = %d",bpageId,dialogResponse);
	
    // default action is to reject cert
	sslInfo.setAcceptDecision(0);

	//TODO: should really enum these response constants
	
	if (dialogResponse == 0) {
		g_debug("BrowserServer [bpage = %u]: (ssl) user decision = Reject",bpageId);
	}
	else if (dialogResponse == 1) {
		/*	user selected "accept permanently"...Install it into the store, and then I *SHOULD* verify it to add the correct openssl hash (per DustinH's note)
		 * 	sigh, install should really just be doing all that
		 * 
		 */
		g_message("BrowserServer [bpage = %u]: (ssl) user decision = Accept Permanently",bpageId);
		int result = CERT_OK;
		int serial = 0;
		char passPhrase[128] = {'\0'};
		result = CertInstallKeyPackage(sslInfo.getCertFile().c_str(), NULL, passPhrase, &serial);
		if (result == CERT_OK) {
			g_debug("BrowserServer [bpage = %u]: (ssl) user decision = Accept Permanently - Installed cert OK",bpageId);
			//result = CertAddTrustedCert(serial);		//if it doesn't work, don't worry about it
														//it'll just be like "accept once"
			result = CertAddAuthorizedCert(serial);		//(Mar.09.2009 - suggested that this be used instead, to correctly create symlinks to certs)
														
			if (result == CERT_OK) {
				g_debug("BrowserServer [bpage = %u]: (ssl) user decision = Accept Permanently - Added to Trust OK",bpageId);
                sslInfo.setAcceptDecision(1);
			}
			else {
				g_warning("BrowserServer [bpage = %u]: (ssl) user decision = Accept Permanently - Failed to add to Trust",bpageId);
			}
		}
		else {
			g_warning("BrowserServer [bpage = %u]: (ssl) user decision = Accept Permanently - Failed to Install cert",bpageId);
		}
	}
	else {   //if user selected "accept once"
		int result = CERT_OK;
		int serial = 0;
		char passPhrase[128] = {'\0'};
		result = CertInstallKeyPackage(sslInfo.getCertFile().c_str(), NULL, passPhrase, &serial);
		g_message("BrowserServer [bpage = %u]: (ssl) user decision = Accept Temporarily",bpageId);
		result = CertAddTrustedCert(serial);		//if it doesn't work, don't worry about it
		if (result == CERT_OK) {
			g_debug("BrowserServer [bpage = %u]: (ssl) user decision = Accept Temporarily - Added to Trust OK",bpageId);
			//add the serial to a list to be removed later (when the page gets tossed)
			temporaryCertSerials.push_back(serial);
            sslInfo.setAcceptDecision(2);
		}
		else {
			g_warning("BrowserServer [bpage = %u]: (ssl) user decision = Accept Temporarily - Failed to add to Trust",bpageId);
		}
	}

    unlink(sslInfo.getCertFile().c_str());

	freePtrArray(replyArray);

	return true;
}

void 
BrowserPage::mimeHandoffUrl( const char* mimeType, const char* url )
{
	m_server->msgMimeHandoffUrl(m_proxy, mimeType, url);
}

void 
BrowserPage::mimeNotHandled( const char* mimeType, const char* url )
{
	m_server->msgMimeNotSupported(m_proxy, mimeType, url);
}

/**
 * Decide if the application owning this page wants to intercept an attempt by
 * WebKit to navigate to the specified URL.
 *
 * @param url The full URL being navigated to.
 */
bool
BrowserPage::interceptLink(const QUrl& url)
{
	std::list<UrlMatchInfo>::const_iterator i;

	for ( i = m_urlRedirectInfo.begin(); i != m_urlRedirectInfo.end(); ++i ) {
		const UrlMatchInfo& mi = *i;
        if (mi.reCompiled() && regexec(&mi.urlRe, qPrintable(url.toString()), 0, NULL, 0) == 0) {
            if (mi.redirect) {
                m_server->msgUrlRedirected(m_proxy, qPrintable(url.toString()), mi.userData.c_str());
			}
			return mi.redirect;
		}
	}

	return false;
}

/**
 * Should the BrowserServer display images in the web page if that is the main
 * document (not embedded images).
 */
bool 
BrowserPage::displayStandaloneImages() const
{
	// HI wants them to be displayed in the media program. To do this we would
	// return false here and the applicationManager would map the image URL/mime
	// to the correct Luna application. Unfortunately we don't yet have that standalone
	// Luna application we will continue to display standalone images in the main frame.
	return true;
}

bool BrowserPage::shouldHandleScheme(const char* scheme) const
{
	if (NULL == scheme) {
		return false;
	}
	else {
		return !strcasecmp(scheme, "http") ||
			!strcasecmp(scheme, "https") ||
			!strcasecmp(scheme, "about") ||
			!strcasecmp(scheme, "data") || // http://en.wikipedia.org/wiki/Data_URI_scheme
			!strcasecmp(scheme, "file"); // TODO Need to analyze resource for security purposes.
	}
}

/**
 * @brief Looks for interactive nodes in expanded radius around x, y at mouse down.
 *        Gets render rectangles from palmwebview, and sends coords as json string to js.
 * 
 */
void BrowserPage::getInteractiveNodeRects(int32_t mouseX, int32_t mouseY)
{
    if (!m_webPage) {
        BERR("No page created");
        return;
    }
    
    clientPointToServer((uint32_t&)mouseX, (uint32_t&)mouseY);

    std::vector<Palm::WebRect> nodeRects;
#ifdef FIXME_QT
    m_webView->getInteractiveNodeRects(mouseX, mouseY, nodeRects); 
#endif // FIXME_QT

    if (nodeRects.empty()) {
//		printf("BrowserPage::getInteractiveNodeRects: %d, %d is Empty\n", mouseX, mouseY);
        return;
    }
	
//	printf("BrowserPage::getInteractiveNodeRects: %d, %d returned %d rects\n", mouseX, mouseY, nodeRects.size());

    // prepare json object
    json_object* rectsJson = json_object_new_array();
    if (rectsJson == NULL) {
        return;
    }

    // for each rectangle, send 4 coordinates to BA, which will draw the rects

    Palm::WebRect prevRect;  // used to prevent overlapping rects
    prevRect.top = -1;
    prevRect.left = -1;
    prevRect.bottom = -1;
    prevRect.right = -1;
    
    for (std::vector<Palm::WebRect>::iterator rects_iter = nodeRects.begin();
         rects_iter != nodeRects.end(); 
         rects_iter++)
    {
        Palm::WebRect r = *rects_iter;
        g_debug("%s: see rect with left: %d, top: %d, right: %d, bottom: %d\n", 
                __FUNCTION__, r.left, r.top, r.right, r.bottom);

        // construct json string for IntRect
        json_object* rect = json_object_new_object();
        if (rect == NULL) {
            json_object_put(rectsJson);
            return;
        }

        // correct boundaries of vertically overlapping or disconnected rects
        if (prevRect.top != -1 
                && ((prevRect.bottom != r.bottom && prevRect.top != r.top)
                        || (prevRect.right != r.left && r.right != prevRect.left)))
        {
            // overlapping 
            if (prevRect.top < r.top && prevRect.bottom < r.bottom
                    && prevRect.bottom > r.top) {
                // prevRect is above r
                r.top = prevRect.bottom;
            } 
            else if (r.top < prevRect.top && r.bottom < prevRect.bottom
                    && r.bottom > prevRect.top) {
                // prevRect is blow r
                r.bottom = prevRect.top;
            }
            
            // vertically disconnected 
            if (prevRect.bottom < r.top) {
                r.top = prevRect.bottom;
            } else if (r.bottom < prevRect.top) {
                // prevRect is below r
                r.bottom = prevRect.top;
            }
         }
    
        json_object_object_add(rect, "left", json_object_new_int(r.left));
        json_object_object_add(rect, "top", json_object_new_int(r.top));
        json_object_object_add(rect, "right", json_object_new_int(r.right));
        json_object_object_add(rect, "bottom", json_object_new_int(r.bottom));

        json_object_array_add(rectsJson, rect);
        prevRect = r;
    }

    const char* rectsStr = json_object_get_string(rectsJson);

    // send json string to BrowserAdapter
    m_server->msgHighlightRects(m_proxy, rectsStr);

    json_object_put(rectsJson);
    
}

void BrowserPage::mouseEvent(int type, int contentX, int contentY, int detail)
{
    if (!m_webPage) {
        BERR("No page created");
        return;
    }

    bool sendIgnoredEditorFocusedMsg = (m_fingerEventCount == 0 && m_hasFocusedNode);

    handleFingerEvent();

	clientPointToServer((uint32_t&) contentX, (uint32_t&) contentY);

    if(type == 2) { // MouseMove event
        QMouseEvent mouseMove(QEvent::MouseMove, QPoint(contentX, contentY), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(m_graphicsView->viewport(), &mouseMove);
    }

    if (sendIgnoredEditorFocusedMsg)
        editorFocused(m_hasFocusedNode, m_lastEditorState);
    else
        updateEditorFocus();
}

void BrowserPage::gestureEvent(int type, int contentX, int contentY, double scale, double rotation,
							   int centerX, int centerY)
{
    if (!m_webPage) {
        BERR("No page created");
        return;
    }

    handleFingerEvent();

	clientPointToServer((uint32_t&) contentX, (uint32_t&) contentY);

    enum GestureEventType {
        GestureStart,
        GestureChange,
        GestureEnd
    };

    // These values are from the Palm::GestureEventType enumeration from FrankenKit
    const int Palm_GestureStart = 0;
    const int Palm_GestureChange = 1;
    const int Palm_GestureEnd = 2;
    const int Palm_GestureSingleTap = 3;

    QWebEvent::Type evtType;

    switch (type) {
    case Palm_GestureStart:
        evtType = QWebEvent::getIosGestureStartEventType();
        break;
    case Palm_GestureChange:
        evtType = QWebEvent::getIosGestureChangeEventType();
        break;
    case Palm_GestureEnd:
        evtType = QWebEvent::getIosGestureEndEventType();
        break;
    case Palm_GestureSingleTap:
        // This is only needed for the BrowserAdapter so don't need to pass
        // this through to BrowserServer's WebKit.
        return;
    default:
        return;
    }

    // No modifier key states passed with message, so say that none are down.
    QWebIosGestureEvent event(evtType, QPoint(contentX, contentY), rotation, scale,
            false, false, false, false);

    m_webPage->event(&event);
}

void BrowserPage::touchEvent(int type, int32_t touchCount, int32_t modifiers, const char *touchesJson)
{
	static Palm::TouchPointPalm touches[10];
	pbnjson::JSchemaFragment inputSchema("{}");
	pbnjson::JDomParser parser(NULL);
	if (!parser.parse(std::string(touchesJson), inputSchema, NULL)) {
		BERR("error parsing json, dropping touch event");
		return;
	}

	pbnjson::JValue parsed = parser.getDom();
	for (int i = 0; i < touchCount; i++) {
		int x, y, state;
		parsed[i]["x"].asNumber<int>(x);
		parsed[i]["y"].asNumber<int>(y);
		parsed[i]["state"].asNumber<int>(state);
		touches[i].x = x;
		touches[i].y = y;
		touches[i].state = (Palm::TouchPointPalm::State)state;
	}

    handleFingerEvent();

#ifdef FIXME_QT
	m_webView->touchEvent((Palm::TouchEventType)type, touches, touchCount, false, false, false, false);
#endif // FIXME_QT

    updateEditorFocus();
}

void 
BrowserPage::downloadStart( const char* url )
{
		snprintf (buffer, maxTransfer , "\n URL = %s \n" , url);
   
    m_server->msgDownloadStart(m_proxy, url);
}

void 
BrowserPage::downloadProgress( const char* url, unsigned long bytesSoFar, unsigned long estimatedTotalSize )
{
    m_server->msgDownloadProgress(m_proxy, url, bytesSoFar, estimatedTotalSize);
}

void 
BrowserPage::downloadError( const char* url, const char* msg )
{
    m_server->msgDownloadError(m_proxy, url, msg);

    	snprintf (buffer, maxTransfer , "\n URL = %s \n" , url);
}

void 
BrowserPage::downloadFinished( const char* url, const char* mimeType, const char* tmpPath )
{
    m_server->msgDownloadFinished(m_proxy, url, mimeType, tmpPath);

    	snprintf (buffer, maxTransfer , "\n URL = %s \n" , url);
}

void
BrowserPage::downloadCancel( const char* url )
{
    m_webPage->triggerAction(QWebPage::Stop);

		snprintf (buffer, maxTransfer , "\n URL = %s \n" , url);
}


WebOSWebPage*
BrowserPage::createWebOSWebPage(QWebPage::WebWindowType type)
{
    BrowserPage* newPage = new BrowserPage(m_server, m_server->createRecordProxy(), m_server->getServiceHandle()); 
    if (!newPage) {
        BERR("%s: Unable to allocate BrowserPage instance", __FUNCTION__);
        return 0;
    }

    newPage->setIdentifier(getIdentifier());
    
    newPage->init(m_virtualWindowWidth, m_virtualWindowHeight, 0, 0, 0);

	newPage->setWindowSize(m_windowWidth, m_windowHeight);

    // create identifier that will be used to find the BP in the watch list
    // when the app is ready with a BrowserAdapter
    int32_t pageId = createIdentifier();

    // put page on watch list in which a new BrowserAdapter 
    // will find this BrowserPage instance
    BrowserPageManager::instance()->watchForPage(newPage, pageId);

    m_server->msgCreatePage(m_proxy, pageId);
    
    return newPage->m_webPage;
}

int32_t BrowserPage::createIdentifier()
{      
    struct timeval now;
    gettimeofday(&now, NULL);
    
    int32_t id = now.tv_sec % 1000;

    return id;
}

void
BrowserPage::dispatchFailedLoad(const char* domain, int errorCode,
			const char* failingURL, const char* localizedDescription)
{
	if (!m_webPage) {
        // This should never happen
        BERR("No page created");
        return;
    }
    
    m_server->msgFailedLoad(m_proxy, domain, errorCode, failingURL, localizedDescription);
}

void
BrowserPage::setMainDocumentError(const char* domain, int errorCode,
			const char* failingURL, const char* localizedDescription)
{
	if (!m_webPage) {
        // This should never happen
        BERR("No page created");
        return;
    }

    if (m_server->isInternetConnectionAvailable) {
    	m_server->msgSetMainDocumentError(m_proxy, domain, errorCode, failingURL, localizedDescription);
    }
    else {
    	// send "no internet connection" error
    	m_server->msgSetMainDocumentError(m_proxy, domain, Palm::ERR_NO_INTERNET_CONNECTION, failingURL, "No Internet Connection");
    }
}


void
BrowserPage::closePageSoon()
{
// FIXME: Impl    
}


// FIXME: these need to registered as callbacks or as a client interface impl
extern "C" bool
callback_can_render_fonts()
{
    return true;
}

extern "C" bool
app_handle_use_javascript(void)
{
    return true;
}

extern "C" bool
app_handle_enable_images(void)
{
    return true;
}

uint32_t BrowserPage::getPriority()
{
    return m_priority;
}

void BrowserPage::setPriority(uint32_t priority)
{
    m_priority = priority;
}

/**
 * Set the "real" YAP proxy for this browser page. Pages created by the server that are not
 * yet connected to an adapter have an initial YapProxy to record outbound messages until
 * the adapter connects back to the page. The caller of this routine must first transfer
 * the messages from the record proxy to the supplied proxy before making this call as the
 * record proxy will be deleted.
 */
void BrowserPage::setProxy(YapProxy* proxy)
{
	m_server->deleteRecordProxy(m_proxy);
    m_proxy = proxy;

    if (m_needsReloadOnConnect) {
        g_warning("reloading page due to proxy connect issue");
        pageReload();
        m_needsReloadOnConnect = false;
    }
}

YapProxy* BrowserPage::getProxy() 
{
    return m_proxy;
}

static inline bool approxEqual(double a, double b) {
	const double tolerance = 0.00001;
	return fabs(a -b) < tolerance;
}

void BrowserPage::viewportTagParsed(double initialScale, double minimumScale, double maximumScale,
		int width, int height, bool userScalable, bool didUseConstantsForWidth, bool didUseConstantsForHeight)
{
	BDBG("Viewport tag Parsed: initialScale: %g, minimumScale: %g, maximumScale: %g, "
		 "width: %d, height: %d, userScalable: %d",
		 initialScale, minimumScale, maximumScale, width, height, userScalable);

	if (m_ignoreMetaViewport) {
		return;
	}

	if (m_metaViewportSet.enable &&
		approxEqual(m_metaViewportSet.initialScale, initialScale) &&
		approxEqual(m_metaViewportSet.minimumScale, minimumScale) &&
		approxEqual(m_metaViewportSet.maximumScale, maximumScale) &&
		m_metaViewportSet.width == width &&
		m_metaViewportSet.height == height &&
		m_metaViewportSet.userScalable == userScalable) {

		// Already seen this
		return;
	}

	resetMetaViewport();

	m_metaViewportSet.enable = true;
	m_metaViewportSet.initialScale = initialScale;
	m_metaViewportSet.minimumScale = minimumScale;
	m_metaViewportSet.maximumScale = maximumScale;
	m_metaViewportSet.width = width;
	m_metaViewportSet.height = height;
	m_metaViewportSet.userScalable = userScalable;

	

	m_metaViewport.enable = true;

	// --- Clamp values -----------------------------------------------------

	if (initialScale > 0) {
		m_metaViewport.initialScale = CLAMP(initialScale, kMetaViewportMinimumScale, kMetaViewportMaximumScale);
	}

	if (minimumScale > 0) {
		m_metaViewport.minimumScale = CLAMP(minimumScale, kMetaViewportMinimumScale, kMetaViewportMaximumScale);
	}
	else {
		m_metaViewport.minimumScale = kMetaViewportDefaultMinimumScale;
	}

	if (maximumScale > 0) {
		m_metaViewport.maximumScale = CLAMP(maximumScale, kMetaViewportMinimumScale, kMetaViewportMaximumScale);
	}
	else {
		m_metaViewport.maximumScale = kMetaViewportDefaultMaximumScale;
	}

	m_metaViewport.initialScale = CLAMP(m_metaViewport.initialScale,
										m_metaViewport.minimumScale,
										m_metaViewport.maximumScale);

	if (width > 0) {
		m_metaViewport.width = CLAMP(width, m_windowWidth, kMetaViewportMaxWidth);
		if (!didUseConstantsForWidth)
			m_metaViewport.widthEnforced = true;
	}

	if (height > 0) {
		m_metaViewport.height = CLAMP(height, m_windowHeight, kMetaViewportMaxHeight);
		if (!didUseConstantsForHeight)
			m_metaViewport.heightEnforced = true;
	}

	m_metaViewport.userScalable = userScalable;
	if (fabs(m_metaViewport.maximumScale - m_metaViewport.minimumScale) < 0.0001)
		m_metaViewport.userScalable = false;

	// ----------------------------------------------------------------------
	
	int deviceWidth, deviceHeight;
	getScreenSize(deviceWidth, deviceHeight);

	if (m_metaViewport.width <= 0) {
		// Width not set. infer from scale
		m_metaViewport.width = (int) (deviceWidth / m_metaViewport.initialScale);

		// Viewport width no smaller than deviceWidth
		m_metaViewport.width = MAX(m_metaViewport.width, deviceWidth);
	}

	if (m_metaViewport.height <= 0) {
		// Height not set. infer from scale
		m_metaViewport.height = (int) (deviceHeight / m_metaViewport.initialScale);

		// Viewport height no smaller than deviceHeight
		m_metaViewport.height = MAX(m_metaViewport.height, deviceHeight);
	}

	BDBG("Viewport tag Inferred: initialScale: %g, minimumScale: %g, maximumScale: %g, "
		 "width: %d, height: %d, userScalable: %d\n",
		 m_metaViewport.initialScale, m_metaViewport.minimumScale,
		 m_metaViewport.maximumScale, m_metaViewport.width,
		 m_metaViewport.height, m_metaViewport.userScalable);

	m_server->msgMetaViewportSet(m_proxy, m_metaViewport.initialScale, m_metaViewport.minimumScale,
								 m_metaViewport.maximumScale, m_metaViewport.width,
								 m_metaViewport.height, m_metaViewport.userScalable);
	
	// ----------------------------------------------------------------------
}

void BrowserPage::resetMetaViewport()
{
	m_metaViewport.enable = false;
	m_metaViewport.initialScale = 0.0;
	m_metaViewport.maximumScale = 0.0;
	m_metaViewport.minimumScale = 0.0;
	m_metaViewport.width = 0;
	m_metaViewport.height = 0;
	m_metaViewport.userScalable = false;
	m_metaViewport.widthEnforced = false;
	m_metaViewport.heightEnforced = false;

	m_metaViewportSet.enable = false;
	m_metaViewportSet.initialScale = 0.0;
	m_metaViewportSet.maximumScale = 0.0;
	m_metaViewportSet.minimumScale = 0.0;
	m_metaViewportSet.width = 0;
	m_metaViewportSet.height = 0;
	m_metaViewportSet.userScalable = false;
}

/**
 * @return True if node in focus is an editable field, False otherwise
 * 
 */
bool BrowserPage::isEditing()
{
    if (!m_webPage) {
        BERR("No page created");
        return false;
    }
    
#ifdef FIXME_QT
    return m_webView->isEditing();
#else
    return false;
#endif // FIXME_QT
}

void BrowserPage::insertStringAtCursor(const char* text) 
{
    if (!m_webPage) {
        BERR("No page created");
        return;
    }
    
#ifdef FIXME_QT
    m_webView->insertStringAtCursor(text);
#endif // FIXME_QT
}

bool
BrowserPage::saveImageAtPoint(uint32_t x, uint32_t y, QString& filePath)
{
    if (filePath.isEmpty())
        return false;

    QUrl imageUrl = hitTest(x, y).imageUrl();
    if (imageUrl.isEmpty())
        return false;

    QByteArray imageData;
    QAbstractNetworkCache *cache = m_webPage->networkAccessManager()->cache();
    if (cache) {

        QScopedPointer<QIODevice> device (cache->data(imageUrl));
        if (!device.isNull())
            imageData = device->readAll();
    }
    else {

        QNetworkRequest request(imageUrl);
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
        QNetworkReply *reply = m_webPage->networkAccessManager()->get(request);

        QEventLoop loop;
        QObject::connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));

        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, SIGNAL(timeout()), &loop, SLOT(quit()));

        QSettings settings;
        timer.start(settings.value("SaveImageTimeout", 2000).toInt());
        loop.exec();
        timer.stop();

        reply->deleteLater();

        if (reply->error()) {
            BERR("Download of %s failed: %s\n", imageUrl.toEncoded().constData(), qPrintable(reply->errorString()));
            return false;
        }

        if (reply->isFinished())
            imageData = reply->readAll();
    }

    if (imageData.isEmpty())
        return false;

    QString imageName = QFileInfo(imageUrl.path()).fileName();
    if (imageName.isEmpty())
        return false;

    QDir dir(filePath);
    if (!dir.exists())
        dir.mkpath(dir.absolutePath());

    filePath = makeUniqueFileName(dir.absoluteFilePath(imageName));
    QFile imageFile (filePath);

    if (!imageFile.open(QIODevice::WriteOnly)) {
        BERR("Could not open %s for writing: %s", qPrintable(filePath), qPrintable(imageFile.errorString()));
        return false;
    }

    if (imageFile.write(imageData) == -1) {
        BERR("Failed writing image data to %s: %s", qPrintable(filePath), qPrintable(imageFile.errorString()));
        return false;
    }

    imageFile.close();
    BDBG("saveImageAtPoint: %s", qPrintable(filePath));
    return true;
}

QWebHitTestResult
BrowserPage::hitTest (uint32_t x, uint32_t y)
{
    clientPointToServer(x, y);
    return m_webView->page()->mainFrame()->hitTestContent(QPoint(x, y));
}

void
BrowserPage::selectionChanged()
{
	if (m_server) {
		int left(0), top(0), right(0), bottom(0);
        getTextCaretBounds(left, top, right, bottom);
		m_server->msgGetTextCaretBoundsResponse(m_proxy, 0, left, top, right, bottom);
	}
}

void
BrowserPage::copiedToClipboard()
{
    if (m_server) {
        m_server->msgCopiedToClipboard(m_proxy);
    }
}

void
BrowserPage::pastedFromClipboard()
{
    if (m_server) {
        m_server->msgPastedFromClipboard(m_proxy);
    }
}

// request from plugin to js
void BrowserPage::pluginFullscreenSpotlightCreate(int handle, int rectx, int recty, int rectw, int recth)
{
	if (m_server) {
		m_server->msgPluginFullscreenSpotlightCreate(m_proxy, handle, rectx, recty, rectw, recth);
	}
}

// request from plugin to js
void BrowserPage::pluginFullscreenSpotlightRemove()
{
	if (m_server) {
		m_server->msgPluginFullscreenSpotlightRemove(m_proxy);
	}
}

json_object* BrowserPage::rectToJson(uintptr_t id, int x, int y, int width, int height, Palm::InteractiveRectType type)
{
    json_object* rectsJson = json_object_new_array();
    if (!rectsJson || is_error(rectsJson))
        return NULL;

    //syslog(LOG_DEBUG, "%s: rect with left: %d, top: %d, right: %d, bottom: %d\n",
    //       __FUNCTION__, x, y, x+width, y+height);

    // construct json string for IntRect
    json_object* rect = json_object_new_object();
    if (!rect || is_error(rect)) {
        json_object_put(rectsJson);
        return NULL;
    }

    json_object_object_add(rect, "id", json_object_new_int(id));
    json_object_object_add(rect, "left", json_object_new_int(x));
    json_object_object_add(rect, "top", json_object_new_int(y));
    json_object_object_add(rect, "right", json_object_new_int(x + width));
    json_object_object_add(rect, "bottom", json_object_new_int(y + height));
    json_object_object_add(rect, "type", json_object_new_int((int)type));

    json_object_array_add(rectsJson, rect);

    return rectsJson;
}

void BrowserPage::addInteractiveWidgetRect(uintptr_t id, int x, int y, int width, int height, Palm::InteractiveRectType type)
{
    json_object* rectsJson = rectToJson(id, x, y, width, height, type);

    if (!rectsJson || is_error(rectsJson))
        return;

    const char* rectsStr = json_object_get_string(rectsJson);

   //syslog(LOG_DEBUG, "%s: rectsStr: %s\n",
   //        __FUNCTION__, rectsStr);

    // send json string to BrowserAdapter
    m_server->msgAddFlashRects(m_proxy, rectsStr);

    json_object_put(rectsJson);
}

void BrowserPage::removeInteractiveWidgetRect(uintptr_t id, Palm::InteractiveRectType type)
{
    json_object* rectsJson = json_object_new_object();

    if (!rectsJson || is_error(rectsJson))
        return;

    json_object_object_add(rectsJson, "id", json_object_new_int(id));
    json_object_object_add(rectsJson, "type", json_object_new_int((int)type));

    const char* rectsStr = json_object_get_string(rectsJson);

    //syslog(LOG_DEBUG, "%s: rectsStr: %s\n",
    //       __FUNCTION__, rectsStr);

    // send json string to BrowserAdapter
    m_server->msgRemoveFlashRects(m_proxy, rectsStr);

    json_object_put(rectsJson);
}

/**
 * The context information for our smart key search.
 */
struct SmartKeySearchContext
{
	int requestId;
};

/**
 * Called by luna service when a response to a smart key search is received.
 */
bool BrowserPage::smartKeySearchCallback(LSHandle *sh, LSMessage *message, void *ctx)
{
	if (!message) {
		return true;
	}
#ifdef FIXME_QT
	int requestId = (int)ctx;
#endif

	const char* payload = LSMessageGetPayload(message);
	if (!payload)
		return true;

	bool succeeded(false);
	
	json_object* json = json_tokener_parse(payload);

	if (ValidJsonObject(json)) {
		json_object* value = json_object_object_get(json, "returnValue" );
		if( ValidJsonObject(value) && json_object_get_boolean(value) ) {
			value = json_object_object_get(json, "match" );
			if( ValidJsonObject(value) ) {
#ifdef FIXME_QT
				Palm::WebGlobal::smartKeySearchResponse(requestId, json_object_get_string(value));
#endif
				succeeded = true;
			}
		}
		json_object_put(json);
	}

	if (!succeeded) {
#ifdef FIXME_QT
		Palm::WebGlobal::smartKeySearchResponse(requestId, "");
#endif
	}

	return true;
}

bool BrowserPage::smartKeyLearn(const char* word)
{
	if (!m_lsHandle || !word || *word == '\0') {
		return false;
	}

	json_object* payload = json_object_new_object();
	if (!ValidJsonObject(payload)) {
		return false;
	}

	json_object_object_add(payload, "word", json_object_new_string(word));

	LSError error;
	LSErrorInit(&error);

	bool succeeded = LSCall(m_lsHandle, "palm://com.palm.smartKey/learn", json_object_get_string(payload),
				 NULL, NULL, NULL, &error);

	json_object_put(payload);

	if (!succeeded) {
		g_warning("Failed querying smartKey service: %s", error.message);
		LSErrorFree(&error);
		return false;
	}

	return true;
}

/**
 * Called by WebKit to conduct a smart key search.
 */
bool BrowserPage::smartKeySearch(int requestId, const char* query)
{
#ifdef FIXME_QT
	if (m_lsHandle == NULL || query == NULL || *query == '\0')
		return Palm::WebGlobal::smartKeySearchResponse(requestId, "");
#endif

	json_object* payload = json_object_new_object();
	if (!ValidJsonObject(payload)) {
#ifdef FIXME_QT
		return Palm::WebGlobal::smartKeySearchResponse(requestId, "");
#endif
	}

	json_object_object_add(payload, "query", json_object_new_string(query));

	LSError error;
	LSErrorInit(&error);

	bool succeeded = LSCall(m_lsHandle, "palm://com.palm.smartKey/search", json_object_get_string(payload),
				 smartKeySearchCallback, (void*)requestId, NULL, &error);
	json_object_put(payload);
	if (succeeded) {
		return true;
	}
	else {
		g_warning("Failed querying smartKey service: %s", error.message);
		LSErrorFree(&error);
#ifdef FIXME_QT
		return Palm::WebGlobal::smartKeySearchResponse(requestId, "");
#else
        return false;
#endif
	}
}

// event of spotlight from js to plugin
void BrowserPage::pluginSpotlightStart(int rectx, int recty, int rectw, int recth)
{
#ifdef FIXME_QT
	if (m_webView) {
		Palm::WebRect rect;
		rect.left = rectx;
		rect.top = recty;
		rect.right = rectx+rectw;
		rect.bottom =recty+recth;
		m_webView->pluginSpotlightStart(rect);
	}
#endif // FIXME_QT
}

// event of spotlight from js to plugin
void BrowserPage::pluginSpotlightEnd()
{
#ifdef FIXME_QT
	if (m_webView) {
		m_webView->pluginSpotlightEnd();
	}
#endif // FIXME_QT
}

void BrowserPage::hideSpellingWidget()
{
#ifdef FIXME_QT
	if (m_webView) {
		m_webView->hideSpellingWidget();
	}
#endif // FIXME_QT
}

void BrowserPage::hideClipboardWidget(bool resetSelection)
{
#ifdef FIXME_QT
    if (m_webView)
        m_webView->hideClipboardWidget(resetSelection);
#endif // FIXME_QT
}

void BrowserPage::openSearchUrl(const char* url)
{
	if (m_lsHandle)
	{
		LSError error;
		LSErrorInit(&error);
		json_object* payload = json_object_new_object();
		if (!ValidJsonObject(payload)) {
			g_warning("Failed to add to Opensearch URL ,unable to create valid JSON object");
			return ;
		}
		json_object_object_add(payload, "xmlUrl", json_object_new_string(url));
		bool ret = LSCall(m_lsHandle,
			  "palm://com.palm.universalsearch/addOptionalSearchDesc", json_object_get_string(payload),
				 NULL,NULL, NULL, &error);
		if (!ret) {
			g_warning("Failed to add to Opensearch URL to //com.palm.universalsearch: %s",
					  error.message);
			LSErrorFree(&error);
		}

	}
}

void BrowserPage::spellingWidgetVisibleRectUpdate(int x, int y, int width, int height)
{
	if (m_server)
		m_server->msgSpellingWidgetVisibleRectUpdate(m_proxy, x, y, width, height);
}

void BrowserPage::disableEnhancedViewport(bool disable)
{
#ifdef FIXME_QT
	 if (m_webPage != NULL) {
        m_webPage->setEnhancedViewportEnabled(!disable);
    }
#endif // FIXME_QT
}
void BrowserPage::setIgnoreMetaRefreshTags(bool ignore)
{
	
#ifdef FIXME_QT
	 if (m_webPage != NULL) {
        m_webPage->setIgnoreMetaRefreshTags(ignore);
    }
#endif // FIXME_QT
}

void BrowserPage::setIgnoreMetaViewport(bool ignore)
{
	m_ignoreMetaViewport = ignore;
}

void  BrowserPage::setNetworkInterface(const char* interfaceName)
{
	
#ifdef FIXME_QT
	if (m_webPage != NULL) {
        m_webPage->forceNetworkInterface(interfaceName);
    }
#endif // FIXME_QT
}

void  BrowserPage::setDNSServers(const char *servers)
{
#ifdef FIXME_QT
    if (m_webPage != NULL) {
        m_webPage->forceDNSServers(servers);
    }
#endif // FIXME_QT
}

/**
 * Prints the specified frame.  WebKit will render the frame to image file(s) and 
 * notify the Print Manager Luna Service to send them to the printer.
 *
 * @param frameName string
 *   The name of the frame to print.  If not specified, WebKit will render the main frame.
 * @param lpsJobId int
 *   The Luna Print Service job ID associated with this print
 * @param width int
 *   The printable width of the page in pixels
 * @param height int
 *   The printable height of the page in pixels
 * @param dpi int
 *   The print Dpi
 * @param landscape bool
 *   The orientation of the page, TRUE=landscape, FALSE=portrait
 * @param reverseOrder bool
 *   The order to print pages, TRUE=last-to-first, FALSE=first-to-last
 *
 */
void BrowserPage::printFrame(const char* frameName, int lpsJobId, int width, int height, int dpi, bool landscape, bool reverseOrder)
{
    g_debug("%s %s %d %d %d %d %s %s", __PRETTY_FUNCTION__, frameName, lpsJobId, width, height, dpi, landscape?"ls":"pt", reverseOrder?"up":"down");
#ifdef FIXME_QT
    if (m_webPage != NULL && m_webView != NULL) {
        m_webView->print(frameName, lpsJobId, width, height, dpi, landscape, reverseOrder);
    }
#endif // FIXME_QT
}

void BrowserPage::showPrintDialog()
{
	m_server->msgShowPrintDialog(m_proxy);
}

void BrowserPage::setZoomAndScroll(double zoom, int cx, int cy)
{
	cx = cx / zoom;
	cy = cy / zoom;
	
	if (PrvIsEqual(zoom, m_zoomLevel) && cx == m_pageX && cy == m_pageY)
		return;

	cx = MAX(0, cx);
	cy = MAX(0, cy);
	
	m_pageX = cx;
	m_pageY = cy;
	m_zoomLevel = zoom;

//    QTransform scale;
//    scale.scale(m_zoomLevel, m_zoomLevel);
//qDebug() << " ### Setting scale:" << m_zoomLevel;
//    m_webView->setTransform(scale);

	// FIXME: RR. WebKit currently does a fit width scale. we override it
	// by setting the scale to 1
//	m_webPage->mainFrame()->setZoomFactor(1.0f);
		
	updateContentScrollParamsForOffscreen();
}

void BrowserPage::invalidate()
{
    m_webView->update();
}

void BrowserPage::updateContentScrollParamsForOffscreen()
{
    if (PrvZoomNotSet(m_zoomLevel) || m_pageWidth == 0 || m_pageHeight == 0)
        m_offscreenCalculations.reset();
    else {
        
        calculateContentParamsForOffscreen(m_zoomLevel,
                                           m_pageWidth * m_zoomLevel,
                                           m_pageHeight * m_zoomLevel,
                                           m_windowWidth, m_windowHeight);

        calculateScrollParamsForOffscreen(m_pageX * m_zoomLevel,
                                          m_pageY * m_zoomLevel);
    }

    QTransform scale;
    scale.scale(m_offscreenCalculations.contentZoom, m_offscreenCalculations.contentZoom);
    m_webView->setTransform(scale);
    m_graphicsView->setFixedSize(m_offscreenCalculations.bufferWidth, m_offscreenCalculations.bufferHeight);
    m_graphicsView->setSceneRect(0, 0, m_offscreenCalculations.bufferWidth, m_offscreenCalculations.bufferHeight);
    m_webView->page()->setActualVisibleContentRect(QRect(m_offscreenCalculations.renderX / m_offscreenCalculations.contentZoom,
                                                         m_offscreenCalculations.renderY / m_offscreenCalculations.contentZoom,
                                                         m_offscreenCalculations.renderWidth / m_offscreenCalculations.contentZoom,
                                                         m_offscreenCalculations.renderHeight / m_offscreenCalculations.contentZoom));
    m_webView->page()->mainFrame()->setScrollPosition(QPoint(m_pageX, m_pageY));

    m_webView->update();
}

// To be called whenever zoom, page dimensions or viewport dimensions change
void BrowserPage::calculateContentParamsForOffscreen(double zoomLevel,
													 int contentWidth,
													 int contentHeight,
													 int viewportWidth,
													 int viewportHeight)
{
	if (!m_offscreen0)
		return;

	if (viewportWidth == 0 || viewportHeight == 0 ||
		contentWidth == 0 || contentHeight == 0) {
		m_offscreenCalculations.reset();	
		return;
	}

	if (PrvIsEqual(zoomLevel, m_offscreenCalculations.contentZoom) &&
		contentWidth   == m_offscreenCalculations.contentWidth &&
		contentHeight  == m_offscreenCalculations.contentHeight &&
		viewportWidth  == m_offscreenCalculations.viewportWidth &&
		viewportHeight == m_offscreenCalculations.viewportHeight)
		return;
	
	int bufferPixelSize = m_offscreen0->rasterSize() / sizeof(unsigned int);
	assert(viewportWidth * viewportHeight <= bufferPixelSize);

	int optimalWidth = viewportWidth * kOffscreenWidthOverflow;
	optimalWidth = MIN(optimalWidth, contentWidth);
	int optimalHeight = bufferPixelSize / optimalWidth;

	if (optimalHeight < optimalWidth) {
		optimalWidth = viewportWidth;
		optimalHeight = bufferPixelSize / optimalWidth;
	}

	m_offscreenCalculations.reset();

	m_offscreenCalculations.contentZoom = zoomLevel;
	
	m_offscreenCalculations.bufferWidth = optimalWidth;
	m_offscreenCalculations.bufferHeight = optimalHeight;

	m_offscreenCalculations.contentWidth = contentWidth;
	m_offscreenCalculations.contentHeight = contentHeight;
	m_offscreenCalculations.viewportWidth = viewportWidth;
	m_offscreenCalculations.viewportHeight = viewportHeight;
}

// To be called whenever scroll position changes
void BrowserPage::calculateScrollParamsForOffscreen(int contentX, int contentY)
{
	if (!m_offscreen0)
		return;
//printf("\n *** BrowserPage::calculateScrollParamsForOffscreen %d %d\n\n", contentX, contentY);
	BrowserOffscreenCalculations& oc = m_offscreenCalculations;
	if (oc.viewportWidth == 0 ||
		oc.viewportHeight == 0 ||
		oc.contentWidth == 0 ||
		oc.contentHeight == 0)
		return;
	
	contentX = MAX(contentX, 0);
	contentY = MAX(contentY, 0);
	contentX = MIN(contentX, oc.contentWidth - oc.viewportWidth);
	contentY = MIN(contentY, oc.contentHeight - oc.viewportHeight);

	static const int xMargin = 64;
	static const int yMargin = 128;

	// Did we scroll past the current rendered area (or render area is uninitalized)
	if ((oc.renderWidth == 0) ||
		(oc.renderHeight == 0) ||
		(oc.renderX + xMargin > contentX) ||
		(oc.renderY + yMargin > contentY) ||
		((oc.renderX + oc.renderWidth - xMargin) < (contentX + oc.viewportWidth)) ||
		((oc.renderY + oc.renderHeight - yMargin) < (contentY + oc.viewportHeight))) {

		bool fullRepaint = oc.renderWidth == 0 || oc.renderHeight == 0;
		QRect oldRect(oc.renderX, oc.renderY, oc.renderWidth, oc.renderHeight);

		// Center the render region within the content region.
		// Also make sure the render region doesn't shoot past the edges
		oc.renderX = contentX + oc.viewportWidth / 2 - oc.bufferWidth / 2;
		oc.renderX = MIN(oc.renderX, oc.contentWidth - oc.bufferWidth);
		oc.renderX = MAX(oc.renderX, 0);

		oc.renderY = contentY + oc.viewportHeight / 2 - oc.bufferHeight / 2;
		oc.renderY = MIN(oc.renderY, oc.contentHeight - oc.bufferHeight);
		oc.renderY = MAX(oc.renderY, 0);
				
		oc.renderWidth = oc.bufferWidth;
		oc.renderHeight = oc.contentHeight - oc.renderY;
		oc.renderHeight = MIN(oc.renderHeight, oc.bufferHeight);

		if (fullRepaint) {
			invalidate();
		}
	}	
}

void BrowserPage::initKeyMap() {

    if (keyMapInit)
        return;

    keyMap[Key_Escape]     = Qt::Key_Escape;
    keyMap[Key_Tab]        = Qt::Key_Tab;
    keyMap[Key_Delete]     = Qt::Key_Delete;
    keyMap[Key_Backspace]  = Qt::Key_Backspace;
    keyMap[Key_Return]     = Qt::Key_Return;
    keyMap[Key_Return]     = Qt::Key_Enter;
    keyMap[Key_Insert]     = Qt::Key_Insert;
    keyMap[Key_Delete]     = Qt::Key_Delete;
    keyMap[Key_MediaPause] = Qt::Key_Pause;
    keyMap[Key_Home]       = Qt::Key_Home;
    keyMap[Key_End]        = Qt::Key_End;
    keyMap[Key_Left]       = Qt::Key_Left;
    keyMap[Key_Up]         = Qt::Key_Up;
    keyMap[Key_Right]      = Qt::Key_Right;
    keyMap[Key_Down]       = Qt::Key_Down;
    keyMap[Key_PageUp]     = Qt::Key_PageUp;
    keyMap[Key_PageDown]   = Qt::Key_PageDown;
    keyMap[Key_Shift]      = Qt::Key_Shift;
    keyMap[Key_Ctrl]       = Qt::Key_Control;
    keyMap[Key_Option]     = Qt::Key_Meta;
    keyMap[Key_Alt]        = Qt::Key_Alt;

    keyMap[Key_F1]         = Qt::Key_F1;
    keyMap[Key_F2]         = Qt::Key_F2;
    keyMap[Key_F3]         = Qt::Key_F3;
    keyMap[Key_F4]         = Qt::Key_F4;
    keyMap[Key_F5]         = Qt::Key_F5;
    keyMap[Key_F6]         = Qt::Key_F6;
    keyMap[Key_F7]         = Qt::Key_F7;
    keyMap[Key_F8]         = Qt::Key_F8;
    keyMap[Key_F9]         = Qt::Key_F9;
    keyMap[Key_F10]        = Qt::Key_F10;
    keyMap[Key_F11]        = Qt::Key_F11;
    keyMap[Key_F12]        = Qt::Key_F12;

    keyMap[Key_CoreNavi_Back]        = Qt::Key_CoreNavi_Back;
    keyMap[Key_CoreNavi_Menu]        = Qt::Key_CoreNavi_Menu;
    keyMap[Key_CoreNavi_QuickLaunch] = Qt::Key_CoreNavi_QuickLaunch;
    keyMap[Key_CoreNavi_Launcher]    = Qt::Key_CoreNavi_Launcher;
    keyMap[Key_CoreNavi_Down]        = Qt::Key_CoreNavi_SwipeDown;
    keyMap[Key_CoreNavi_Next]        = Qt::Key_CoreNavi_Next;
    keyMap[Key_CoreNavi_Previous]    = Qt::Key_CoreNavi_Previous;
    keyMap[Key_CoreNavi_Home]        = Qt::Key_CoreNavi_Home;
    keyMap[Key_CoreNavi_Meta]        = Qt::Key_CoreNavi_Meta;
    keyMap[Key_Flick]                = Qt::Key_Flick;

    keyMap[Key_Slider]               = Qt::Key_Slider;
    keyMap[Key_Optical]              = Qt::Key_Optical;
    keyMap[Key_Ringer]               = Qt::Key_Ringer;
    keyMap[Key_HardPower]            = Qt::Key_Power;
    keyMap[Key_HeadsetButton]        = Qt::Key_HeadsetButton;
    keyMap[Key_Headset]              = Qt::Key_Headset;
    keyMap[Key_HeadsetMic]           = Qt::Key_HeadsetMic;

    keyMapInit = true;
}

void BrowserPage::setCanBlitOnScroll(bool val)
{
}

void BrowserPage::didLayout()
{
#ifdef FIXME_QT
	if (m_webView) {

		std::list<Palm::ScrollableLayerItem> layers = m_webView->getScrollableLayers();

		if (m_scrollableLayerItems.empty()) {
			for (std::list<Palm::ScrollableLayerItem>::const_iterator it = layers.begin();
				 it != layers.end(); ++it) {
				m_scrollableLayerItems[it->id] = *it;
			}
		}
		else {

			bool changed = false;

			std::set<uintptr_t> allOldIds;
			for (ScrollableLayerItemMap::const_iterator it = m_scrollableLayerItems.begin();
				 it != m_scrollableLayerItems.end(); ++it) {

				allOldIds.insert(it->first);
			}
 
			for (std::list<Palm::ScrollableLayerItem>::const_iterator it = layers.begin();
				 it != layers.end(); ++it) {

				ScrollableLayerItemMap::iterator iter = m_scrollableLayerItems.find(it->id);
				if (iter == m_scrollableLayerItems.end()) {
					changed = true;
					m_scrollableLayerItems[it->id] = *it;
				}
				else {

					Palm::ScrollableLayerItem& oldItem = iter->second;
					const Palm::ScrollableLayerItem& newItem = *it;

					if (newItem.absoluteBounds.left != oldItem.absoluteBounds.left ||
						newItem.absoluteBounds.top != oldItem.absoluteBounds.top ||
						newItem.absoluteBounds.right != oldItem.absoluteBounds.right ||
						newItem.absoluteBounds.bottom != oldItem.absoluteBounds.bottom ||
						newItem.hasHorizontalBar != oldItem.hasHorizontalBar ||
						(newItem.hasHorizontalBar && newItem.horizontalBarData.total != oldItem.horizontalBarData.total) ||
						newItem.hasVerticalBar != oldItem.hasVerticalBar ||
						(newItem.hasVerticalBar && newItem.verticalBarData.total != oldItem.verticalBarData.total)) {

						oldItem = newItem;
						changed = true;
					}

					allOldIds.erase(oldItem.id);
				}
			}

			for (std::set<uintptr_t>::const_iterator it = allOldIds.begin();
				 it != allOldIds.end(); ++it) {

				changed = true;
				m_scrollableLayerItems.erase(*it);
			}

			if (!changed)
				return;
		}
			

		json_object* json = json_object_new_array();
		for (std::list<Palm::ScrollableLayerItem>::const_iterator it = layers.begin();
			 it != layers.end(); ++it) {

			json_object* obj = json_object_new_object();

			json_object_object_add(obj, "id", json_object_new_int(it->id));
			
			json_object_object_add(obj, "left", json_object_new_int(it->absoluteBounds.left));
			json_object_object_add(obj, "top", json_object_new_int(it->absoluteBounds.top));
			json_object_object_add(obj, "right", json_object_new_int(it->absoluteBounds.right));
			json_object_object_add(obj, "bottom", json_object_new_int(it->absoluteBounds.bottom));

			int contentX = 0;
			int contentY = 0;
			int contentWidth = it->absoluteBounds.right - it->absoluteBounds.left;
			int contentHeight = it->absoluteBounds.bottom - it->absoluteBounds.top;
			
			if (it->hasHorizontalBar) {
				contentX = it->horizontalBarData.value;
				contentWidth = it->horizontalBarData.total;
			}

			if (it->hasVerticalBar) {
				contentY = it->verticalBarData.value;
				contentHeight = it->verticalBarData.total;
			}

			json_object_object_add(obj, "contentX", json_object_new_int(contentX));
			json_object_object_add(obj, "contentY", json_object_new_int(contentY));
			json_object_object_add(obj, "contentWidth", json_object_new_int(contentWidth));
			json_object_object_add(obj, "contentHeight", json_object_new_int(contentHeight));

			json_object_array_add(json, obj);
		}

		const char* str = json_object_get_string(json);
		m_server->msgUpdateScrollableLayers(m_proxy, str);
		
		json_object_put(json);
	}
#endif // FIXME_QT
}

void BrowserPage::doContentsSizeChanged(const QSize& size) {
    resizedContents(size.width(), size.height());
}

void BrowserPage::scrollLayer(int id, int deltaX, int deltaY)
{
#ifdef FIXME_QT
	if (m_webView)
		m_webView->scrollLayer((void*) id, deltaX, deltaY);
#endif // FIXME_QT
}
void BrowserPage::doSelectionChanged()
{
    QRect selectStart,selectEnd;
    if(!m_webPage->hasSelection() || !m_webPage->selectionBounds(selectStart,selectEnd,m_selectionRect))
        return;

    uint32_t topMarkerX = selectStart.x();
    uint32_t topMarkerY = selectStart.y();
    uint32_t bottomMarkerX = selectEnd.x();
    uint32_t bottomMarkerY = selectEnd.y()+selectEnd.height();

    //FIXME: CUTE-166: selection on zoomed page may required further investigation
    clientPointToServer(topMarkerX, topMarkerY);
    clientPointToServer(bottomMarkerX, bottomMarkerY);

    m_topMarker->setOffset(topMarkerX-(m_topMarker->boundingRect().width()/2), topMarkerY-m_topMarker->boundingRect().height());
    m_bottomMarker->setOffset(bottomMarkerX-(m_bottomMarker->boundingRect().width()/2), bottomMarkerY);
    m_topMarker->setVisible(true);
    m_bottomMarker->setVisible(true);

    removeInteractiveWidgetRect((uintptr_t)m_topMarker,Palm::InteractiveRectDefault);
    removeInteractiveWidgetRect((uintptr_t)m_bottomMarker,Palm::InteractiveRectDefault);
    addInteractiveWidgetRect((uintptr_t)m_topMarker,
            m_topMarker->offset().x()-selectMarkerExtraPixels,
            m_topMarker->offset().y()-selectMarkerExtraPixels,
            m_topMarker->boundingRect().width()+selectMarkerExtraPixels,
            m_topMarker->boundingRect().height()+selectMarkerExtraPixels,
            Palm::InteractiveRectDefault);
    addInteractiveWidgetRect((uintptr_t)m_bottomMarker,
            m_bottomMarker->offset().x()-selectMarkerExtraPixels,
            m_bottomMarker->offset().y()-selectMarkerExtraPixels,
            m_bottomMarker->boundingRect().width()+selectMarkerExtraPixels,
            m_bottomMarker->boundingRect().height()+selectMarkerExtraPixels,
            Palm::InteractiveRectDefault);

}
void BrowserPage::loadSelectionMarkers()
{
    if(m_topMarker && m_bottomMarker)
        return;
    QSettings settings;
    const QString imageResourcesPath = settings.value("ImageResourcesPath").toString();
    if(!m_topMarker)
        m_topMarker= new QGraphicsPixmapItem(QPixmap(imageResourcesPath+topMarkerImg));
    if(!m_bottomMarker)
        m_bottomMarker = new QGraphicsPixmapItem(QPixmap(imageResourcesPath+bottomMarkerImg));

    hideSelectionMarkers();

    m_scene->addItem(m_topMarker);
    m_scene->addItem(m_bottomMarker);
}
void BrowserPage::hideSelectionMarkers()
{
    m_topMarker->setVisible(false);
    m_bottomMarker->setVisible(false);
    removeInteractiveWidgetRect((uintptr_t)m_topMarker,Palm::InteractiveRectDefault);
    removeInteractiveWidgetRect((uintptr_t)m_bottomMarker,Palm::InteractiveRectDefault);

}
