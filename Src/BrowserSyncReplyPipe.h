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

#ifndef BROWSERSYNCREPLYPIPE_H
#define BROWSERSYNCREPLYPIPE_H

#include <glib.h>

class BrowserPage;

class BrowserSyncReplyPipe
{
public:

    BrowserSyncReplyPipe(BrowserPage* page);
    ~BrowserSyncReplyPipe();

    const char* pipePath() const;

    bool getReply(GPtrArray** reply, int mainSocketFd);

private:

    bool readFull(char* buf, int len, int fd);

    static gboolean socketCallback(GIOChannel* channel, GIOCondition condition, gpointer arg);
    static gboolean pipeCallback(GIOChannel* channel, GIOCondition condition, gpointer arg);

    char* m_pipePath;
    char* m_replyBuffer;
    GMainLoop* m_mainLoop;
    GMainContext* m_mainCtxt;
    bool m_pipeReadFailed;
};

#endif /* BROWSERSYNCREPLYPIPE_H */
