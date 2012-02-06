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

#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <byteswap.h>
#include <sys/stat.h>

#include "YapDefs.h"
#include "YapPacket.h"
#include "YapClient.h"

int YapClient::s_socketNum = 0;

static const int   kMaxPathLen       = 256;
static const char* kSocketPathPrefix = "/tmp/yapserver.";
static const int   kMaxConnections   = 10;

class YapClientPriv
{
public:

    GMainLoop*    mainLoop;
    GMainContext* mainCtxt;

    char          cmdSocketPath[kMaxPathLen];
    int           cmdSocketFd;
    GIOChannel*   cmdIoChannel;
    GSource*      cmdIoSource;

    char*         msgServerSocketPostfix;
    char          msgServerSocketPath[kMaxPathLen];
    int           msgServerSocketFd;
    GIOChannel*   msgServerIoChannel;
    GSource*      msgServerIoSource;

    int           msgSocketFd;
    GIOChannel*   msgIoChannel;
    GSource*      msgIoSource;

    uint8_t*      msgBuffer;
    uint8_t*      cmdBuffer;
    uint8_t*      replyBuffer;
 
    YapPacket*    msgPacket;
    YapPacket*    cmdPacket;
    YapPacket*    replyPacket;

    YapClientPriv()
        : mainLoop(0)
        , mainCtxt(0)
        , cmdSocketFd(-1)
        , cmdIoChannel(0)
        , cmdIoSource(0)
        , msgServerSocketPostfix(0)
        , msgServerSocketFd(-1)
        , msgServerIoChannel(0)
        , msgServerIoSource(0)
        , msgSocketFd(-1)
        , msgIoChannel(0)
        , msgIoSource(0)
        , msgBuffer(0)
        , cmdBuffer(0)
        , replyBuffer(0)
        , msgPacket(0)
        , cmdPacket(0)
        , replyPacket(0) {;}

 

    static gboolean ioCallback(GIOChannel* channel, GIOCondition condition, void* data)
    {
        YapClient* client = (YapClient*) data;
        client->ioCallback(channel, condition);
        return TRUE;
    }
};

/*
 * This constructor allocates a dedicated GMainContext & GMainLoop for this object,
 * through which messages will be received.  It must be used by calling run(), or
 * nothing will be received.
 */
YapClient::YapClient(const char* name)
{
    d = new YapClientPriv;

    // Set up the main loop
    d->mainCtxt = g_main_context_new();
    d->mainLoop = g_main_loop_new(d->mainCtxt, TRUE);

    init(name);
}

/*
 * This constructor will use the given GMainContext, or the default if ctxt == NULL.
 * run() must not be called -- in this configuration, we assume the YapClient will be
 * used with an external GMainLoop of some sort.
 */
YapClient::YapClient(const char* name, GMainContext *ctxt)
{
    d = new YapClientPriv;


    // Set up the main loop
    if(ctxt == NULL)
        ctxt = g_main_context_default();

    d->mainLoop = 0;
    d->mainCtxt = ctxt;

    init(name);
}

YapClient::~YapClient()
{
    if(d->msgServerIoSource != NULL) {
        g_source_destroy(d->msgServerIoSource);
        d->msgServerIoSource = NULL;
    }
    if(d->msgServerIoChannel != NULL) {
        g_io_channel_unref(d->msgServerIoChannel);
        d->msgServerIoChannel = NULL;
    }
    if(d->msgServerSocketFd != -1) {
        close(d->msgServerSocketFd);
        d->msgServerSocketFd = -1;
    }

    if (d->msgServerSocketPostfix)
        free(d->msgServerSocketPostfix);

    ::unlink(d->msgServerSocketPath);

    closeMsgSocket();
    closeCmdSocket();

    // We use mainLoop != NULL as a test to tell whether we allocated a loop & context during init().
    // In the case where we're using the default main context, we never allocate a loop.
    if(d->mainLoop != NULL)
    {
        g_main_loop_unref(d->mainLoop);
        g_main_context_unref(d->mainCtxt);
    }

    delete d->msgPacket;
    delete d->cmdPacket;
    delete d->replyPacket;

    delete[] d->msgBuffer;
    delete[] d->cmdBuffer;
    delete[] d->replyBuffer;

    delete d;

    d = NULL;
}

bool YapClient::connect()
{
    // connect to remote server

    struct sockaddr_un socketAddr;

    d->cmdSocketFd = ::socket(PF_LOCAL, SOCK_STREAM, 0);
    if (d->cmdSocketFd < 0)
        return false;

    memset(&socketAddr, 0, sizeof(socketAddr));
    socketAddr.sun_family = AF_LOCAL;
    strncpy(socketAddr.sun_path, d->cmdSocketPath, G_N_ELEMENTS(socketAddr.sun_path));
    socketAddr.sun_path[G_N_ELEMENTS(socketAddr.sun_path)-1] = '\0';

    if (::connect(d->cmdSocketFd, (struct sockaddr*) &socketAddr,
                  SUN_LEN(&socketAddr)) != 0) {
        close(d->cmdSocketFd);
        d->cmdSocketFd = -1;
        fprintf(stderr, "YAP: Failed to connect to server\n");
        return false;
    }

    // send our msg server socket path
    int16_t strLen = ::strlen(d->msgServerSocketPath);
    int16_t pktLen = bswap_16(strLen);
    if (!writeSocket(d->cmdSocketFd, (char*) &pktLen, 2))
        return false;

    if (!writeSocket(d->cmdSocketFd, d->msgServerSocketPath, strLen))
        return false;

    strLen = ::strlen(d->msgServerSocketPostfix);
    pktLen = bswap_16(strLen);

    if (!writeSocket(d->cmdSocketFd, (char*) &pktLen, 2))
        return false;

    if (!writeSocket(d->cmdSocketFd, d->msgServerSocketPostfix, strLen))
        return false;

    // Add io channel to know when the command socket is disconnected.
    d->cmdIoChannel = g_io_channel_unix_new(d->cmdSocketFd);
    d->cmdIoSource  = g_io_create_watch(d->cmdIoChannel, (GIOCondition) (G_IO_HUP));

    g_source_set_callback(d->cmdIoSource, (GSourceFunc) YapClientPriv::ioCallback, this, NULL);
    g_source_attach(d->cmdIoSource, d->mainCtxt);

    return true;
}

GMainLoop* YapClient::mainLoop() const
{
    return d->mainLoop;
}

YapPacket* YapClient::packetCommand()
{
    d->cmdPacket->reset();
    return d->cmdPacket;
}

YapPacket* YapClient::packetReply()
{
    d->replyPacket->reset();
    return d->replyPacket;
}

const char* YapClient::incrementPostfix()
{
    if (d->msgServerSocketPostfix) {
        free(d->msgServerSocketPostfix);
        d->msgServerSocketPostfix = 0;
    }

    if (::asprintf(&(d->msgServerSocketPostfix), "%d-%d", getpid(), s_socketNum++) <= 0)
        d->msgServerSocketPostfix = 0;

    return postfix();
}

const char* YapClient::postfix() const
{
    return d->msgServerSocketPostfix;
}

bool YapClient::sendAsyncCommand()
{
    char pktHeader[4] = { 0, 0, 0, 0 };
    uint16_t pktLen = 0;

    if (d->cmdPacket->length() == 0) {
        fprintf(stderr, "Command is empty\n");
        return false;
    }

    pktLen = d->cmdPacket->length();
    pktLen = bswap_16(pktLen);
    ::memset(pktHeader, 0, sizeof(pktHeader));
    ::memcpy(pktHeader, &pktLen, 2);

    bool succeeded = writeSocket(d->cmdSocketFd, pktHeader, 4);
    if (succeeded) {
        succeeded = writeSocket(d->cmdSocketFd, (char*) d->cmdBuffer, d->cmdPacket->length());
    }

    return succeeded;
}

bool YapClient::sendSyncCommand()
{
    char pktHeader[4] = { 0, 0, 0, 0 };
    uint16_t pktLen;
    char * ppp = 0;

    if (d->cmdPacket->length() == 0) {
        fprintf(stderr, "Command is empty\n");
        return false;
    }

    pktLen = 0;
    ::memset(pktHeader, 0, sizeof(pktHeader));

    pktLen = d->cmdPacket->length();
    pktLen = bswap_16(pktLen);
    ::memcpy(pktHeader, &pktLen, 2);
    pktHeader[3] = kPacketFlagSyncMask;

    if (!writeSocket(d->cmdSocketFd, pktHeader, 4))
        goto Detached;

    if (!writeSocket(d->cmdSocketFd, (char*) d->cmdBuffer, d->cmdPacket->length()))
        goto Detached;


    pktLen = 0;
    ::memset(pktHeader, 0, sizeof(pktHeader));

    if (!readSocketSync(d->cmdSocketFd, pktHeader, 4))
        goto Detached;

    ppp = ((char *)&pktHeader[0]);
    pktLen   = *((uint16_t*) ppp);
    pktLen   = bswap_16(pktLen);
    if (pktLen > kMaxMsgLen) {
        fprintf(stderr, "YAP: Message too large %u > %u\n", pktLen, kMaxMsgLen);
        goto Detached;
    }

    if (!readSocketSync(d->cmdSocketFd, (char*) d->replyBuffer, pktLen))
        goto Detached;

    d->replyPacket->reset();
    d->replyPacket->setReadTotalLength(pktLen);

    return true;

 Detached:

    serverDisconnected();
    closeMsgSocket();
    closeCmdSocket();

    return false;
}

bool YapClient::run()
{
    if(d->mainLoop == NULL && d->mainCtxt != NULL)
    {
        fprintf(stderr, "YAP: Don't call run() when configured to use external main loop.\n");
        return true;
    }

    // now run the main loop
    g_main_loop_run(d->mainLoop);

    return true;
}

// Finishes object setup for both constructors.
void YapClient::init(const char* name)
{
    d->cmdSocketPath[0] = '\0';
    d->cmdSocketFd = -1;
    d->cmdIoChannel = 0;
    d->cmdIoSource = 0;

    d->msgServerSocketPath[0] = '\0';
    d->msgServerSocketFd = -1;
    d->msgServerIoChannel = 0;
    d->msgServerIoSource = 0;

    d->msgSocketFd = -1;
    d->msgIoChannel = 0;
    d->msgIoSource = 0;


    d->msgBuffer = new uint8_t[kMaxMsgLen];
    d->cmdBuffer = new uint8_t[kMaxMsgLen];
    d->replyBuffer = new uint8_t[kMaxMsgLen];

    d->msgPacket = new YapPacket(d->msgBuffer, 0);
    d->cmdPacket = new YapPacket(d->cmdBuffer);
    d->replyPacket = new YapPacket(d->replyBuffer, 0);

    ::snprintf(d->cmdSocketPath, G_N_ELEMENTS(d->cmdSocketPath), "%s%s", kSocketPathPrefix, name);

    if (!incrementPostfix()) {
        fprintf(stderr, "Format failed\n");
        return;
    }

    if (::snprintf(d->msgServerSocketPath, G_N_ELEMENTS(d->msgServerSocketPath), "%s%s%s", kSocketPathPrefix, name, d->msgServerSocketPostfix) <= 0) {
        fprintf(stderr, "Format failed\n");
        return;
    }
    ::unlink(d->msgServerSocketPath);

    // Set up the msg server socket
    struct sockaddr_un  socketAddr;

    d->msgServerSocketFd = ::socket(PF_LOCAL, SOCK_STREAM, 0);
    if (d->msgServerSocketFd < 0) {
        fprintf(stderr, "YAP: Failed to create socket: %s\n", strerror(errno));
        return;
    }

    socketAddr.sun_family = AF_LOCAL;
    strncpy(socketAddr.sun_path, d->msgServerSocketPath, G_N_ELEMENTS(socketAddr.sun_path));
    socketAddr.sun_path[G_N_ELEMENTS(socketAddr.sun_path)-1] = '\0';
    if (::bind(d->msgServerSocketFd, (struct sockaddr*) &socketAddr, SUN_LEN(&socketAddr)) != 0) {
        fprintf(stderr, "YAP: Failed to bind socket: %s\n", strerror(errno));
        d->msgServerSocketFd = -1;
        return;
    }

    ::chmod( socketAddr.sun_path, S_IRWXU | S_IRWXG | S_IRWXO );

    if (::listen(d->msgServerSocketFd, kMaxConnections) != 0) {
        fprintf(stderr, "YAP: Failed to listen on socket: %s\n", strerror(errno));
        d->msgServerSocketFd = -1;
        return;
    }


    // Add io channel for incoming connection from remote server for messages
    d->msgServerIoChannel = g_io_channel_unix_new(d->msgServerSocketFd);
    d->msgServerIoSource  = g_io_create_watch(d->msgServerIoChannel, (GIOCondition) (G_IO_IN | G_IO_HUP));

    g_source_set_callback(d->msgServerIoSource, (GSourceFunc) YapClientPriv::ioCallback, this, NULL);
    g_source_attach(d->msgServerIoSource, d->mainCtxt);
}

void YapClient::ioCallback(GIOChannel* channel, GIOCondition condition)
{
    if (d == NULL) {
        fprintf(stderr, "YAP: ioCallback was executing on a null YapClientPrv!");
        return;
    }

    if (channel == d->msgServerIoChannel) {
        // Server connected to our message socket. Created a new msg socket for it

        struct sockaddr_un  socketAddr;
        socklen_t           socketAddrLen;

        memset(&socketAddr,    0, sizeof(socketAddr));
        memset(&socketAddrLen, 0, sizeof(socketAddrLen));

        d->msgSocketFd = ::accept(d->msgServerSocketFd, (struct sockaddr*) &socketAddr, &socketAddrLen);

        // Create a new io channel for the receiving messages from the server
        d->msgIoChannel = g_io_channel_unix_new(d->msgSocketFd);
        d->msgIoSource  = g_io_create_watch(d->msgIoChannel, (GIOCondition) (G_IO_IN | G_IO_HUP));

        g_source_set_callback(d->msgIoSource, (GSourceFunc) YapClientPriv::ioCallback, this, NULL);
        g_source_attach(d->msgIoSource, d->mainCtxt);
    }
    else if (channel == d->cmdIoChannel) {
        if (condition & G_IO_HUP) {
            g_message("YAP: Server disconnected command socket");
            closeMsgSocket();
            closeCmdSocket();
            serverDisconnected();
        }
    }
    else if (channel == d->msgIoChannel) {

        char pktHeader[4] = { 0, 0, 0, 0 };
        uint16_t pktLen = 0;
        char * ppp = 0;

        // We either got a message or a server disconnect
        if (condition & G_IO_HUP)
            goto Detached;

        // Get the packet header
        if (!readSocket(d->msgSocketFd, pktHeader, 4)) {
            fprintf(stderr, "YAP: Failed to read packet header\n");
            goto Detached;
        }

        ppp = ((char *)&pktHeader[0]);
        pktLen   = *((uint16_t*) ppp);
        pktLen   = bswap_16(pktLen);

        if (pktLen > kMaxMsgLen) {
            fprintf(stderr, "YAP: ERROR packet length too large: %u > %d\n", pktLen, kMaxMsgLen);
            goto Detached;
        }

        // Get the packet data
        if (!readSocket(d->msgSocketFd, (char*) d->msgBuffer, pktLen)) {
            fprintf(stderr, "YAP: Failed to read packet data of length: %d\n", pktLen);
            goto Detached;
        }

        d->msgPacket->reset();
        d->msgPacket->setReadTotalLength(pktLen);

        handleAsyncMessage(d->msgPacket);

        d->msgPacket->reset();
        d->msgPacket->setReadTotalLength(0);

        return;

    Detached:

        serverDisconnected();
        closeMsgSocket();
        closeCmdSocket();
    }
}

bool YapClient::readSocket(int fd, char* buf, int len)
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

bool YapClient::readSocketSync(int fd, char* buf, int len)
{
    int index = 0;

    while (len) {
        int count = ::read(fd, &buf[index], len);
        if (count <= 0) {
            fprintf(stderr, "Failed to read from reply pipe. Error: %d, %s\n", errno, strerror(errno));
            return false;
        }

        index  += count;
        len    -= count;
    }

    return true;
}

bool YapClient::writeSocket(int fd, char* buf, int len)
{
    int index = 0;

    if (fd == -1)
        return false;

    while (len > 0) {
        int count = ::write(fd, &buf[index], len);
        if (count <= 0) {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            else {
                fprintf(stderr, "Failed to write to socket. Error: %d, %s\n", errno, strerror(errno));
                return false;
            }
        }

        index  += count;
        len    -= count;
    }

    return true;
}


void YapClient::closeMsgSocket(void)
{
    if(d->msgIoSource != NULL) {
        g_source_destroy(d->msgIoSource);
        d->msgIoSource = NULL;
    }
    if(d->msgIoChannel != NULL) {
        g_io_channel_shutdown(d->msgIoChannel, TRUE, NULL);
        g_io_channel_unref(d->msgIoChannel);
        d->msgIoChannel = NULL;
    }
    if(d->msgSocketFd != -1) {
        close(d->msgSocketFd);
        d->msgSocketFd = -1;
    }

    return;
}

void YapClient::closeCmdSocket(void)
{
    if(d->cmdIoSource != NULL) {
        g_source_destroy(d->cmdIoSource);
        d->cmdIoSource = NULL;
    }
    if(d->cmdIoChannel != NULL) {
        g_io_channel_shutdown(d->cmdIoChannel, TRUE, NULL);
        g_io_channel_unref(d->cmdIoChannel);
        d->cmdIoChannel = NULL;
    }
    if(d->cmdSocketFd != -1) {
        close(d->cmdSocketFd);
        d->cmdSocketFd = -1;
    }
}
