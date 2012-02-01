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

#ifndef YAPSERVER_H
#define YAPSERVER_H

#include <stdint.h>
#include <glib.h>

class YapServerPriv;
class YapProxy;
class YapPacket;

class YapServer
{
public:

    YapServer(const char* name);
    virtual ~YapServer();

    GMainLoop* mainLoop() const;
    void       run(int deadlockTimeoutMs = -1);
    void       pauseDeadlockDetection();
    void       resumeDeadlockDetection();
    void       incrementDeadlockCounter();
    void       setDeadlockDetectionInterval(const int IntervalInSeconds);
	YapProxy*  createRecordProxy();
	void       deleteRecordProxy(YapProxy* proxy);
	int        serverSocketFd() const;

    virtual void clientConnected(YapProxy* proxy) = 0;
    virtual void clientDisconnected(YapProxy* proxy) = 0;
    virtual void handleAsyncCommand(YapProxy* proxy, YapPacket* cmd) = 0;
    virtual void handleSyncCommand(YapProxy* proxy, YapPacket* cmd, YapPacket* reply) = 0;

private:

    void init();
    void ioCallback(GIOChannel* channel, GIOCondition condition);
    bool readSocket(int fd, char* buf, int len);

    YapServerPriv* d;

    // Copy not allowed
    YapServer(const YapServer&);
    YapServer& operator=(const YapServer&);
    
    friend class YapServerPriv;
};

#endif /* YAPSERVER_H */
