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

#ifndef YAPPROXY_H
#define YAPPROXY_H

#include <stdint.h>
#include <glib.h>
#include <queue>

class YapServer;
class YapPacket;

class YapProxy
{
public:

    const char* postfix() const { return m_msgSocketPostfix; }
    void  setPrivateData(void* priv);
    void* privateData() const;
    void  setTerminate();
    bool  isRecordProxy() const;

    YapPacket* packetMessage();
    void sendMessage();
    void transferQueuedMessage(YapProxy* srcProxy);

    bool connected() const;

    int messageSocketFd() const;
    int commandSocketFd() const;
    int serverSocketFd() const;

private:

    struct Message {
        char     hdr[4]; ///< Message header
        uint8_t* data;   ///< Message data
        int      len;    ///< Message data length

        Message( const char* hdr, const uint8_t* data, int dataLen );
        ~Message();

        private:

        Message* operator=(const Message& rhs);
        Message(const Message& rhs);
    };

    YapProxy(YapServer* server, int cmdSocketFd, char* msgSocketPath, char* msgSocketPostfix);
    ~YapProxy();

    void ioFunction(GIOChannel* channel, GIOCondition condition);
    bool readSocket(int fd, char* buf, int len);
    bool writeSocket(int fd, char* buf, int len);

    YapServer*  m_server;
    int         m_cmdSocketFd;
    int         m_msgSocketFd;
    char*       m_msgSocketPostfix;
    void*       m_privData;
    uint8_t*    m_cmdBuffer;
    uint8_t*    m_replyBuffer;
    uint8_t*    m_msgBuffer;
    GIOChannel* m_ioChannel;
    GSource*    m_ioSource;

    bool        m_inSyncMode;
    bool        m_terminate;
    std::queue<Message*> m_queuedMessages; ///< Messages sent before connection go here.

    YapPacket*  m_packetCommand;
    YapPacket*  m_packetReply;
    YapPacket*  m_packetMessage;

    // Copy not allowed
    YapProxy(const YapProxy&);
    YapProxy& operator=(const YapProxy&);

    friend class YapServer;
    friend gboolean YapProxyIoFunction(GIOChannel* channel, GIOCondition condition, void* data);
};

#endif /* YAPPROXY_H */
