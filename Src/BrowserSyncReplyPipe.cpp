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

#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "BrowserSyncReplyPipe.h"

#define kMaxPathLen  256
#define kMaxReplyLen 4096

BrowserSyncReplyPipe::BrowserSyncReplyPipe(BrowserPage* page)
{
    m_pipePath = new char[kMaxPathLen];
    ::snprintf(m_pipePath, kMaxPathLen, "/tmp/browserSyncReply.%p", page);

    ::unlink(m_pipePath);
    
    ::umask(0);
    ::mknod(m_pipePath, S_IFIFO|0666, 0);

    m_replyBuffer = new char[kMaxReplyLen];
	m_mainLoop = 0;
	m_mainCtxt = 0;
	m_pipeReadFailed = false;	
}

BrowserSyncReplyPipe::~BrowserSyncReplyPipe()
{
    if (m_pipePath) {
        ::unlink(m_pipePath);
        delete [] m_pipePath;
    }

    if (m_replyBuffer)
        delete [] m_replyBuffer;
}

const char*
BrowserSyncReplyPipe::pipePath() const
{
    return m_pipePath;
}

bool
BrowserSyncReplyPipe::getReply(GPtrArray** replyArray, int mainSocketFd)
{
    if (!m_replyBuffer)
        return false;

    int pipeFd = ::open(m_pipePath, O_RDONLY | O_NONBLOCK);
    if (pipeFd < 0) {
        fprintf(stderr, "Failed to open reply pipe. Error: %s\n", strerror(errno));
        return false;
    }

	m_pipeReadFailed = false;
	
	// Wait for message to show up. also watch the main Socket Fd
	// to watch if connection gets broken

	m_mainCtxt = g_main_context_new();
	m_mainLoop = g_main_loop_new(m_mainCtxt, TRUE);

	GIOChannel* pipeChannel = g_io_channel_unix_new(pipeFd);	
	GSource* pipeFdSrc = g_io_create_watch(pipeChannel, (GIOCondition) (G_IO_IN));
	g_source_set_callback(pipeFdSrc, (GSourceFunc) pipeCallback, this, NULL);
	g_source_attach(pipeFdSrc, m_mainCtxt);
	g_source_unref(pipeFdSrc);
	
	GIOChannel* socketChannel = g_io_channel_unix_new(mainSocketFd);
	GSource* socketFdSrc = g_io_create_watch(socketChannel, (GIOCondition) (G_IO_HUP));
	g_source_set_callback(socketFdSrc, (GSourceFunc) socketCallback, this, NULL);
	g_source_attach(socketFdSrc, m_mainCtxt);
	g_source_unref(socketFdSrc);

	g_main_loop_run(m_mainLoop);

	g_io_channel_unref(pipeChannel);
	g_io_channel_unref(socketChannel);

	g_main_loop_unref(m_mainLoop);
	g_main_context_unref(m_mainCtxt);
	m_mainLoop = 0;
	m_mainCtxt = 0;

	if (m_pipeReadFailed) {
		g_warning("Sync reply pipe or socket broken");
		::close(pipeFd);
		m_pipeReadFailed = false;
		return false;
	}

    // Get the message length
    int msgLen = 0;
    if (!readFull((char*) &msgLen, sizeof(int), pipeFd)) {
        ::close(pipeFd);
        return false;
    }

    msgLen = (((msgLen >> 24) & 0xFF) <<  0) |
             (((msgLen >> 16) & 0xFF) <<  8) |
             (((msgLen >>  8) & 0xFF) << 16) |
             (((msgLen >>  0) & 0xFF) << 24);

    // Read the message sent to us
    if (!readFull(m_replyBuffer, msgLen, pipeFd)) {
        ::close(pipeFd);
        return false;
    }

    ::close(pipeFd);
    
    *replyArray = g_ptr_array_new();
    if (!*replyArray)
        return false;

    // Split the message into individual strings
    int index = 0;
    while (index < msgLen) {
        int strLen = ::strlen(&m_replyBuffer[index]);
        char* str  = (char*) ::malloc(strLen + 1);

        if (!str) {
            if (*replyArray) {
                for (unsigned int i = 0; i < (*replyArray)->len; i++) {
                    char* str = (char*) g_ptr_array_index((*replyArray), i);
                    ::free(str);
                }
                
                g_ptr_array_free(*replyArray, TRUE);
            }
            return false;
        }

        if (strLen) {
            ::memcpy(str, &m_replyBuffer[index], strLen);
        }
        str[strLen] = '\0';
        g_ptr_array_add(*replyArray, str);

        index += strLen + 1;
    }

    return true;
}

bool
BrowserSyncReplyPipe::readFull(char* buf, int len, int fd)
{
    int   remaining = len;
    int   index     = 0;

	struct pollfd fds[1];
	::memset(fds, 0, sizeof(fds));
	fds[0].fd = fd;
	fds[0].events = POLLIN;

	errno = 0;
    while (remaining) {

		int ret = poll(fds, G_N_ELEMENTS(fds), -1 /*forever*/);
		if (ret > 0) {
			if (fds[0].revents & POLLIN) {
				int count = ::read(fd, &buf[index], remaining);
				if (count <= 0 && errno != EAGAIN && errno != EINTR) {
					fprintf(stderr, "Failed to read from reply pipe. Error: %d, %s (%d)\n", errno, strerror(errno), count);
					return false;
				}

				index     += count;
				remaining -= count;
			}
			else if (fds[0].revents == POLLHUP) {
				// This happens when a pipe is reopened quickly after closing it. It only happens on device.
				// Not sure if this is expected behavior, but this is a safe albeit unelegent workaround.
				usleep(200000);	// Sleep 200 msec.
			}
		}
		else if (ret == 0) {
			return false; // Shouldn't ever happen.
		}
		else if (ret < 0) {
			fprintf(stderr, "Failed to read from reply pipe. Error: %d, %s\n", errno, strerror(errno));
			return false;
		}
    }

    return true;
}

gboolean
BrowserSyncReplyPipe::socketCallback(GIOChannel* channel, GIOCondition condition, gpointer arg)
{
	BrowserSyncReplyPipe* b = (BrowserSyncReplyPipe*) arg;
	if (condition & G_IO_HUP ||
		condition & G_IO_ERR ||
		condition & G_IO_NVAL) {
		g_warning("BrowserSyncReplyPipe::socketCallback: error on main socket");
		b->m_pipeReadFailed = true;
		g_main_loop_quit(b->m_mainLoop);
    }

	return TRUE;
}

gboolean
BrowserSyncReplyPipe::pipeCallback(GIOChannel* channel, GIOCondition condition, gpointer arg)
{
	BrowserSyncReplyPipe* b = (BrowserSyncReplyPipe*) arg;
	if (condition & G_IO_IN) {
		b->m_pipeReadFailed = false;
		g_main_loop_quit(b->m_mainLoop);
	}

	return TRUE;
}
