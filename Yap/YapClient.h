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

#ifndef YAPCLIENT_H
#define YAPCLIENT_H

#include <stdint.h>
#include <glib.h>

class YapPacket;
class YapClientPriv;

class YapClient
{
public:

    // Allocates GMainContext & GMainLoop internally, used via run().
    YapClient(const char* name);

    // Uses given GMainContext, or default if NULL. Don't call run().
    YapClient(const char* name, GMainContext *ctxt);

    virtual ~YapClient();

    bool connect();

    GMainLoop* mainLoop() const;
    bool run();

    YapPacket* packetCommand();
    YapPacket* packetReply();

    const char* incrementPostfix();
    const char* postfix() const;

    bool sendAsyncCommand();
    bool sendSyncCommand();

    virtual void serverConnected() = 0;
    virtual void serverDisconnected() = 0;
    virtual void handleAsyncMessage(YapPacket* msg) = 0;

private:

    void init(const char* name);
    void ioCallback(GIOChannel* channel, GIOCondition condition);
    bool readSocket(int fd, char* buf, int len);
    bool readSocketSync(int fd, char* buf, int len);
    bool writeSocket(int fd, char* buf, int len);
    void closeMsgSocket(void);
    void closeCmdSocket(void);

    static int s_socketNum;
    YapClientPriv* d;

    // Copy not allowed
    YapClient(const YapClient&);
    YapClient& operator=(const YapClient&);

    friend class YapClientPriv;
};

#endif /* YAPCLIENT_H */
