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

#include <unistd.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <byteswap.h>
#include <sys/stat.h>
#include <pthread.h>

#include "YapProxy.h"
#include "YapServer.h"

#include <QApplication>

static const int   kMaxPathLen       = 256;
static const char* kSocketPathPrefix = "/tmp/yapserver.";
static const int   kMaxConnections   = 100;

// Minimum that is good for performance/power reasons and 32-bit (signed) counter overflow
static const int kDeadlockTimeoutMinSecs = 10;

class YapServerDeadlockPriv
{
public:
    YapServerDeadlockPriv(int deadlockTimeoutMs = 15000);
    ~YapServerDeadlockPriv();
    void            attach(GMainContext *context);
    void            pause();
    void            resume();
    void            increment();
    bool            isPaused() const;
    void            setThreadTimeout(const gint intervalInMs);

private:
    pthread_t       threadId;
    gint            paused; // accessed by multiple threads
    int             deadlockTimerSourceTimeoutMs;
    gint            deadlockThreadTimerSourceTimeoutMs; // accessed by multiple threads
    gint            watchdogCount;    // accessed by multiple threads
    GSource*        deadlockTimerSource;
    GMainContext*   threadMainContext;
    GMainLoop*      threadMainLoop;
    GSource*        threadTimeoutSource;
    static gboolean deadlockTimerCallback(gpointer data);
    static void*    deadlockThread(void *arg);
    static gboolean deadlockThreadTimerCallback(gpointer data);
};

class YapServerPriv
{
public:

    char          socketPath[kMaxPathLen];
    int           socketFd;
    GMainLoop*    mainLoop;
    GMainContext* mainCtxt;
    GIOChannel*   ioChannel;
    GSource*      ioSource;

    static gboolean ioCallback(GIOChannel* channel, GIOCondition condition, void* data);

    YapServerDeadlockPriv* deadlockDetector;
};

YapServerDeadlockPriv::YapServerDeadlockPriv(int deadlockTimeoutMs)
    : threadId(0)
    , paused(0)
    , watchdogCount(0)
    , deadlockTimerSource(0)
    , threadMainContext(0)
    , threadMainLoop(0)
    , threadTimeoutSource(0)
{
    if (deadlockTimeoutMs < kDeadlockTimeoutMinSecs * 1000) {
        g_warning("Attempted to set deadlock timeout to less than %d second minimum; "
                  "using %d second minimum", kDeadlockTimeoutMinSecs, kDeadlockTimeoutMinSecs);
        deadlockTimeoutMs = kDeadlockTimeoutMinSecs * 1000;
    }

    // The deadlock timer running in its separate thread fires more slowly than the
    // one on the mainloop so that we avoid timer aliasing
    deadlockThreadTimerSourceTimeoutMs = deadlockTimeoutMs; // Thread hasn't started yet, okay to set directly.
    deadlockTimerSourceTimeoutMs = deadlockTimeoutMs / 2;
}

YapServerDeadlockPriv::~YapServerDeadlockPriv()
{
    g_main_loop_quit(threadMainLoop);
    pthread_join(threadId, NULL);
}

void YapServerDeadlockPriv::attach(GMainContext *context)
{
    deadlockTimerSource = g_timeout_source_new(deadlockTimerSourceTimeoutMs);
    g_source_set_callback(deadlockTimerSource, deadlockTimerCallback, this, NULL);
    g_source_set_priority(deadlockTimerSource, G_PRIORITY_HIGH);
    g_source_attach(deadlockTimerSource, context);
}

void YapServerDeadlockPriv::pause()
{
    g_atomic_int_set(&paused, 1);
}

void YapServerDeadlockPriv::resume()
{
    g_atomic_int_set(&paused, 0);
}

void YapServerDeadlockPriv::increment()
{
    g_atomic_int_inc(&watchdogCount);
}

bool YapServerDeadlockPriv::isPaused() const
{
    return g_atomic_int_get(&paused) ? true : false;
}

void YapServerDeadlockPriv::setThreadTimeout(const gint intervalInMs)
{
    if (g_atomic_int_get(&deadlockThreadTimerSourceTimeoutMs) < intervalInMs) {

        g_atomic_int_set(&deadlockThreadTimerSourceTimeoutMs, intervalInMs);
    }
}

gboolean YapServerDeadlockPriv::deadlockTimerCallback(gpointer data)
{
    static bool firstCallback = true;

    YapServerDeadlockPriv* d = (YapServerDeadlockPriv*)data;
    g_atomic_int_inc(&d->watchdogCount);

    //g_debug("deadlockTimerCallback!, count: %d", (int)d->watchdogCount);

    if (firstCallback) {
        pthread_create(&d->threadId, NULL, d->deadlockThread, d);
        firstCallback = false;
    }

    return TRUE;
}

gboolean YapServerDeadlockPriv::deadlockThreadTimerCallback(gpointer data)
{
   YapServerDeadlockPriv* d = (YapServerDeadlockPriv*)data;
   static gint lastCountSeen = 0;
   static gint lastInterval = -1;

   const gint interval = g_atomic_int_get(&(d->deadlockThreadTimerSourceTimeoutMs));

   if (lastInterval < interval) {

       if (lastInterval == -1) {
           lastInterval = interval;
       }
       else {
           lastInterval = interval;
           deadlockThread(d);
           return FALSE; // since the interval has changed, wait for next time out to run test;
       }
   }

   // The timers used by glib are monotonic, but count time during
   // suspend (see NOV-112456). As a result, both this callback and the mainloop callback
   // could fire at the same time and incorrectly think there is a deadlock. To avoid
   // this we check the time that has elapsed with the monotonic clock, which doesn't
   // count time during suspend.
   static int64_t lastTimeMs = 0;
   struct timespec now;
   clock_gettime(CLOCK_MONOTONIC, &now);
   int64_t nowMs = now.tv_sec * 1000 + now.tv_nsec / 1000000;
   if (nowMs - lastTimeMs < interval) {
       g_warning("Deadlock detection timer fired too early");
       return TRUE;
   }
   lastTimeMs = nowMs;

   if (d->isPaused()) {
       //g_debug("deadlockThreadTimerCallback: paused");
       return TRUE;
   }

   gint curCount = g_atomic_int_get(&d->watchdogCount);

   //g_debug("deadlockThreadTimerCallback: curCount: %d, lastCountSeen: %d, interval: %d", (int)curCount, (int)lastCountSeen, (int)interval);

   if (curCount <= lastCountSeen && curCount != 0) {
       // we're wedged so we give up and crash
       g_critical("Deadlock detected; aborting!");
       abort();
   }

   lastCountSeen = curCount;

   return TRUE;
}

void* YapServerDeadlockPriv::deadlockThread(void *arg)
{
    YapServerDeadlockPriv* d = (YapServerDeadlockPriv*)arg;

    if (d->threadTimeoutSource) {

       g_source_unref(d->threadTimeoutSource);
       g_source_destroy(d->threadTimeoutSource);
       d->threadTimeoutSource = 0;
    }

    if (!d->threadMainContext) {
        d->threadMainContext = g_main_context_new();
    }

    if (!d->threadMainLoop) {
        d->threadMainLoop = g_main_loop_new(d->threadMainContext, FALSE);
    }
    d->threadTimeoutSource = g_timeout_source_new(g_atomic_int_get (&(d->deadlockThreadTimerSourceTimeoutMs)));
    g_source_set_callback(d->threadTimeoutSource, d->deadlockThreadTimerCallback, d, NULL);
    g_source_attach(d->threadTimeoutSource, d->threadMainContext);

    g_main_loop_run(d->threadMainLoop);

    return NULL;
}

gboolean YapServerPriv::ioCallback(GIOChannel* channel, GIOCondition condition, void* data)
{
    YapServer* server = (YapServer*) data;
    server->ioCallback(channel, condition);
    return TRUE;
}

YapServer::YapServer(const char* name)
{
    d = new YapServerPriv;
    d->socketPath[0] = '\0';
    d->socketFd      = -1;
    d->mainLoop      = 0;
    d->mainCtxt      = 0;
    d->ioSource      = 0;

	::snprintf(d->socketPath, G_N_ELEMENTS(d->socketPath), "%s%s", kSocketPathPrefix, name);
    ::unlink(d->socketPath);

    init();  
}

YapServer::~YapServer()
{
    if (d->deadlockDetector) {
        delete d->deadlockDetector;
    }
    delete d;    
}

void YapServer::init()
{
	struct sockaddr_un  socketAddr;
    
    d->socketFd = ::socket(PF_LOCAL, SOCK_STREAM, 0);
    if (d->socketFd < 0) {
        fprintf(stderr, "YAP: Failed to create socket: %s\n", strerror(errno));
        return;
    }

    socketAddr.sun_family = AF_LOCAL;
    ::strncpy(socketAddr.sun_path, d->socketPath, G_N_ELEMENTS(socketAddr.sun_path));
	socketAddr.sun_path[G_N_ELEMENTS(socketAddr.sun_path)-1] = '\0';
    if (::bind(d->socketFd, (struct sockaddr*) &socketAddr, SUN_LEN(&socketAddr)) != 0) {
        fprintf(stderr, "YAP: Failed to bind socket: %s\n", strerror(errno));
        d->socketFd = -1;
        return;
    }

    if (::listen(d->socketFd, kMaxConnections) != 0) {
        fprintf(stderr, "YAP: Failed to listen on socket: %s\n", strerror(errno));
        d->socketFd = -1;
        return;
    }
    
    d->mainCtxt = g_main_context_default();
    d->mainLoop = g_main_loop_new(d->mainCtxt, TRUE);

    d->ioChannel = g_io_channel_unix_new(d->socketFd);
    d->ioSource  = g_io_create_watch(d->ioChannel, (GIOCondition) (G_IO_IN | G_IO_HUP));

    g_source_set_callback(d->ioSource, (GSourceFunc) YapServerPriv::ioCallback, this, NULL);
    g_source_attach(d->ioSource, d->mainCtxt);
}

GMainLoop* YapServer::mainLoop() const
{
    return d->mainLoop;
}

void YapServer::run(int deadlockTimeoutMs)
{
    if (!d->mainLoop)
        return;
    
    if (deadlockTimeoutMs > 0) {
        d->deadlockDetector = new YapServerDeadlockPriv(deadlockTimeoutMs);
        if (d->mainCtxt) {
            d->deadlockDetector->attach(d->mainCtxt);
        }
    }

    //g_main_loop_run(d->mainLoop);    
    QApplication::exec();
}

void YapServer::pauseDeadlockDetection()
{
    if (d->deadlockDetector) {
        d->deadlockDetector->pause();
    }
}

void YapServer::resumeDeadlockDetection()
{
    if (d->deadlockDetector) {
        d->deadlockDetector->resume();
    }
}

void YapServer::incrementDeadlockCounter()
{
    if (d->deadlockDetector) {
        d->deadlockDetector->increment();
    }
}

void YapServer::setDeadlockDetectionInterval(const int IntervalInSeconds) {

    if (d->deadlockDetector) {
        d->deadlockDetector->setThreadTimeout(IntervalInSeconds * 1000);
    }
}

void YapServer::ioCallback(GIOChannel* channel, GIOCondition condition)
{
	struct sockaddr_un  socketAddr;
    socklen_t           socketAddrLen;
    int                 socketFd = -1;
    int16_t             msgSocketPathLen = 0;
    char*               msgSocketPath = 0;
    int16_t             msgSocketPostfixLen = 0;
    char*               msgSocketPostfix = 0;

    memset(&socketAddr,    0, sizeof(socketAddr));
    memset(&socketAddrLen, 0, sizeof(socketAddrLen));

    socketFd = ::accept(d->socketFd, (struct sockaddr*) &socketAddr, &socketAddrLen);
	if (-1 == socketFd) {
        fprintf(stderr, "YAP: Failed to accept inbound connection.\n");
        goto Detached;
	}
    if (!readSocket(socketFd, (char*) &msgSocketPathLen, 2)) {
        fprintf(stderr, "YAP: Failed to read message socket name length\n");
        goto Detached;
    }
    msgSocketPathLen = bswap_16(msgSocketPathLen);

    msgSocketPath = (char*) malloc(msgSocketPathLen + 1);
    if (!readSocket(socketFd, msgSocketPath, msgSocketPathLen)) {
        fprintf(stderr, "YAP: Failed to read message socket name\n");
        goto Detached;
    }
    msgSocketPath[msgSocketPathLen] = 0;

    ::chmod( msgSocketPath, S_IRWXU | S_IRWXG | S_IRWXO );

    if (!readSocket(socketFd, (char*) &msgSocketPostfixLen, 2)) {
        fprintf(stderr, "YAP: Failed to read message socket name length\n");
        goto Detached;
    }
    msgSocketPostfixLen = bswap_16(msgSocketPostfixLen);

    msgSocketPostfix = (char*) malloc(msgSocketPostfixLen + 1);
    if (!readSocket(socketFd, msgSocketPostfix, msgSocketPostfixLen)) {
        fprintf(stderr, "YAP: Failed to read message postifx name\n");
        goto Detached;
    }
    msgSocketPostfix[msgSocketPostfixLen] = 0;

    YapProxy*           proxy;
    proxy = new YapProxy(this, socketFd, msgSocketPath, msgSocketPostfix);
    clientConnected(proxy);

    free(msgSocketPath);
    free(msgSocketPostfix);
    return;
    
 Detached:

    if (msgSocketPath)
        free(msgSocketPath);
    if (msgSocketPostfix)
        free(msgSocketPostfix);
	if (-1 != socketFd) {
    	close(socketFd);
    }
}

/**
 * Create a new detached YapProxy that will only record outbound messages and not send
 * them.
 *
 * @return A new YapProxy. The caller now owns this object.
 */
YapProxy* YapServer::createRecordProxy()
{
	return new YapProxy(this, -1, NULL, NULL);
}

void YapServer::deleteRecordProxy(YapProxy* proxy)
{
	if (proxy != NULL) {
		assert(proxy->isRecordProxy());
		delete proxy;
	}
}

bool YapServer::readSocket(int fd, char* buf, int len)
{
    int index = 0;

    while (len > 0) {
        int count = ::read(fd, &buf[index], len);
        if (count <= 0) {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            else {
                fprintf(stderr, "Failed to read from socket. Error: %d, %s\n", errno, strerror(errno));
                return false;
            }
        }

        index  += count;
        len    -= count;
    }

    return true;
}

int YapServer::serverSocketFd() const
{
	return d->socketFd;    
}
