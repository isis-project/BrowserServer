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

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <byteswap.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "YapDefs.h"
#include "YapPacket.h"
#include "YapServer.h"
#include "YapProxy.h"

static const int kMaxConnectTries = 5;

gboolean YapProxyIoFunction(GIOChannel* channel, GIOCondition condition, void* data);

YapProxy::Message::Message( const char* hdr, const uint8_t* data, int dataLen ) : data(NULL)
{
    ::memcpy(this->hdr, hdr, sizeof(this->hdr));
    this->data = new uint8_t[dataLen];
    if (this->data != NULL) {
        ::memcpy(this->data, data, dataLen);
    }
    len = dataLen;
}

YapProxy::Message::~Message()
{
    delete [] data;
}

/**
 * Constructor.
 *
 * @param server The Yap server.
 * @param cmdSocketFd The command socket file descriptor. Can be -1 if there is no command socket.
 * @param The path to the message socket. If NULL then outbound messages will be recorded and
 *            sent later once connected.
 */
YapProxy::YapProxy(YapServer* server, int cmdSocketFd, char* msgSocketPath, char* msgSocketPostfix)
    : m_server(server)
    , m_cmdSocketFd(cmdSocketFd)
    , m_msgSocketFd(-1)
    , m_msgSocketPostfix(0)
    , m_privData(0)
    , m_cmdBuffer(0)
    , m_replyBuffer(0)
    , m_msgBuffer(0)
    , m_ioChannel(0)
    , m_ioSource(0)
    , m_inSyncMode(false)
    , m_terminate(false)
    , m_packetCommand(0)
    , m_packetReply(0)
    , m_packetMessage(0)
{
    if (msgSocketPostfix) {

        int length = strlen(msgSocketPostfix);

        if (length > 0) {

            m_msgSocketPostfix = new char[length + 1];
            strncpy(m_msgSocketPostfix, msgSocketPostfix, length);
            m_msgSocketPostfix[length] = '\0';
        }
    }

    m_cmdBuffer   = new uint8_t[kMaxMsgLen];
    m_replyBuffer = new uint8_t[kMaxMsgLen];
    m_msgBuffer   = new uint8_t[kMaxMsgLen];

    m_packetCommand = new YapPacket(m_cmdBuffer, 0);
    m_packetReply   = new YapPacket(m_replyBuffer);
    m_packetMessage = new YapPacket(m_msgBuffer);

    GMainContext* mainCtxt = g_main_loop_get_context(m_server->mainLoop());

    if (m_cmdSocketFd != -1) {
        m_ioChannel = g_io_channel_unix_new(m_cmdSocketFd);
        m_ioSource  = g_io_create_watch(m_ioChannel, (GIOCondition) (G_IO_IN | G_IO_HUP));

        g_source_set_callback(m_ioSource, (GSourceFunc) YapProxyIoFunction, this, NULL);
        g_source_attach(m_ioSource, mainCtxt);
        g_source_set_priority(m_ioSource, G_PRIORITY_HIGH);
    }

    if (msgSocketPath) {
        struct sockaddr_un socketAddr = {0};
        socketAddr.sun_family = AF_LOCAL;
        if (strlen(msgSocketPath) < sizeof(socketAddr.sun_path)) {
            ::strncpy(socketAddr.sun_path, msgSocketPath, sizeof(socketAddr.sun_path));
            socketAddr.sun_path[sizeof(socketAddr.sun_path)-1] = '\0';
        }
        else {
            fprintf(stderr, "Socket path length too long\n");
            return;
        }
        m_msgSocketFd = ::socket(PF_LOCAL, SOCK_STREAM, 0);

        fprintf(stderr, "Connecting to browser-adapter (client) socket: %s ...\n", msgSocketPath);

        int connectTryCount = 0;
        bool connected = false;
        while (connectTryCount < kMaxConnectTries) {

            if (::connect(m_msgSocketFd, (struct sockaddr*) &socketAddr, SUN_LEN(&socketAddr)) == 0) {
                connected = true;
                break;
            }
            else if (errno == EAGAIN || errno == EINTR || errno == ETIMEDOUT) {

                fprintf(stderr, "Retrying connect. Error was: %s\n", strerror(errno));
                connectTryCount++;
                continue;
            }
            else {

                break;
            }
        }

        if (!connected) {
            fprintf(stderr, "Failed to connect to client's msg socket. Messages will not work\n");
            ::close(m_msgSocketFd);
            m_msgSocketFd = -1;
        }
        else {
            fprintf(stderr, "Connected to client socket\n");
        }
    }
}

YapProxy::~YapProxy()
{
    delete m_msgSocketPostfix;
    m_msgSocketPostfix = 0;

    if (m_ioSource) {
        g_source_destroy(m_ioSource);
    }
    if (m_ioChannel) {
        g_io_channel_shutdown(m_ioChannel, TRUE, NULL);
        g_io_channel_unref(m_ioChannel);
    }

    delete [] m_cmdBuffer;
    delete [] m_replyBuffer;
    delete [] m_msgBuffer;

    delete m_packetCommand;
    delete m_packetReply;
    delete m_packetMessage;

    if (m_msgSocketFd != -1)
        ::close(m_msgSocketFd);

    if (m_cmdSocketFd != -1)
        ::close(m_cmdSocketFd);

    while (!m_queuedMessages.empty()) {
        delete m_queuedMessages.front();
        m_queuedMessages.pop();
    }
}

bool YapProxy::isRecordProxy() const
{
    return m_msgSocketFd == -1;
}

/**
 * Transfer the queued messages from the source proxy to this proxy sending them
 * out.
 */
void YapProxy::transferQueuedMessage(YapProxy* srcProxy)
{
    while (!srcProxy->m_queuedMessages.empty()) {
        Message* message = srcProxy->m_queuedMessages.front();
        srcProxy->m_queuedMessages.pop();

        if (message) {
            bool sent = writeSocket(m_msgSocketFd, message->hdr, sizeof(message->hdr));
            if (sent)
                sent = writeSocket(m_msgSocketFd, (char*)message->data, message->len);

            delete message;

            if (!sent) {
                fprintf(stderr, "Error sending queued message");
            }
        }
    }
}

void YapProxy::setPrivateData(void* priv)
{
    m_privData = priv;
}

void* YapProxy::privateData() const
{
    return m_privData;
}

YapPacket* YapProxy::packetMessage()
{
    m_packetMessage->reset();
    return m_packetMessage;
}

void YapProxy::setTerminate()
{
    m_terminate = true;
}

void YapProxy::sendMessage()
{
    char     pktHeader[4];
    uint16_t pktLen = 0;

    if (m_packetMessage->length() == 0) {
        fprintf(stderr, "Message is empty\n");
        return;
    }

    ::memset(pktHeader, 0, sizeof(pktHeader));

    pktLen = m_packetMessage->length();
    pktLen = bswap_16(pktLen);
    ::memcpy(pktHeader, &pktLen, 2);

    if (m_msgSocketFd != -1) {
        bool sent = writeSocket(m_msgSocketFd, pktHeader, 4);
        if (sent) {
            sent = writeSocket(m_msgSocketFd, (char*) m_msgBuffer, m_packetMessage->length());
        }
        if (!sent) {
            fprintf(stderr, "ERROR sending message, errno=%d.", errno);
        }
    }
    else {
        // Save this message for later and send if socket is opened.
        m_queuedMessages.push(new Message(pktHeader, m_msgBuffer, m_packetMessage->length()));
    }
}

void YapProxy::ioFunction(GIOChannel* channel, GIOCondition condition)
{
    char     pktHeader[4] = { 0, 0, 0, 0 };
    uint16_t pktLen    = 0;
    uint8_t  pktFlags  = 0;
    char * ppp = 0;

    if (condition & G_IO_HUP)
        goto Detached;

    // Get the packet header
    if (!readSocket(m_cmdSocketFd, pktHeader, 4)) {
        fprintf(stderr, "YAP: Failed to read packet header\n");
        goto Detached;
    }
    ppp = ((char *)&pktHeader[0]);
    pktLen   = *((uint16_t*) ppp);
    pktLen   = bswap_16(pktLen);
    pktFlags = (pktHeader[3]);
    if (pktLen > kMaxMsgLen) {
        fprintf(stderr, "YAP: Invalid message length %u > %d\n", pktLen, kMaxMsgLen);
        goto Detached;
    }

    // Get the packet data
    if (!readSocket(m_cmdSocketFd, (char*) m_cmdBuffer, pktLen)) {
        fprintf(stderr, "YAP: Failed to read packet data of length: %d\n", pktLen);
        goto Detached;
    }

    m_inSyncMode = pktFlags & kPacketFlagSyncMask;

    if (m_inSyncMode) {
        m_packetCommand->reset();
        m_packetCommand->setReadTotalLength(pktLen);
        m_packetReply->reset();

        m_server->handleSyncCommand(this, m_packetCommand, m_packetReply);

        pktLen = m_packetReply->length();
        pktLen = bswap_16(pktLen);
        ::memcpy(pktHeader, &pktLen, 2);
        pktHeader[2] = pktFlags;
        pktHeader[3] = 0;

        if (!writeSocket(m_cmdSocketFd, pktHeader, 4)) {
            goto Detached;
        }

        if (m_packetReply->length() > 0) {
            if (!writeSocket(m_cmdSocketFd, (char*) m_replyBuffer, m_packetReply->length())) {
                goto Detached;
            }
        }

        m_packetCommand->reset();
        m_packetCommand->setReadTotalLength(0);
        m_packetReply->reset();
    }
    else {
        m_packetCommand->reset();
        m_packetCommand->setReadTotalLength(pktLen);

        m_server->handleAsyncCommand(this, m_packetCommand);

        m_packetCommand->reset();
        m_packetCommand->setReadTotalLength(0);
    }

    m_inSyncMode = false;

    if (m_terminate) {
        goto Detached;
    }

    return;

 Detached:

    m_server->clientDisconnected(this);
    delete this;
}

bool YapProxy::readSocket(int fd, char* buf, int len)
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

bool YapProxy::writeSocket(int fd, char* buf, int len)
{
    int index = 0;

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

gboolean YapProxyIoFunction(GIOChannel* channel, GIOCondition condition, void* data)
{
    YapProxy* proxy = (YapProxy*) data;
    proxy->ioFunction(channel, condition);
    return TRUE;
}

bool YapProxy::connected() const
{
    return (m_msgSocketFd >= 0);
}

int YapProxy::messageSocketFd() const
{
    return m_msgSocketFd;
}

int YapProxy::commandSocketFd() const
{
    return m_cmdSocketFd;
}

int YapProxy::serverSocketFd() const
{
    return m_server->serverSocketFd();
}
