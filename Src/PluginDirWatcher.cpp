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
#include <sys/inotify.h>
#include "PluginDirWatcher.h"
#include "BrowserServer.h"



struct SourceWatchData {
	int notifyfd;
	int watchfd;
};

static gboolean notifyCallbackFunc(int notifyfd)
{
	char buf[1024];
	const int readBytes = read(notifyfd, buf, G_N_ELEMENTS(buf));
	if (readBytes < 0) {
		perror("read inotify");
		return FALSE;
	}
	else if (readBytes == 0) {
		// buffer too small
	}
	else {
		// I only get the event data so that I can print out a friendly message. Else
		// we could just restart now.
		int i = 0;
		int events = 0;

		while (i < readBytes) {
			const inotify_event* evt = reinterpret_cast<const inotify_event*>(&buf[i]);
			i += sizeof(inotify_event);
			i += evt->len;
			events++;
			if (BrowserServer::instance()->webKitInitialized()) { // No need to restart if WebKit not initialized.
				g_message("Plugin '%s' changed - restarting BrowserServer.", evt->name);
				exit(0);
			}
		}
	}
	
	return TRUE;
}

static gboolean watch_func(GIOChannel *source, GIOCondition condition, gpointer data)
{
    struct SourceWatchData *watchData = static_cast<struct SourceWatchData*>(data);
    if (watchData) {
        return notifyCallbackFunc(watchData->notifyfd);
    }
	else {
    	return FALSE;
	}
}

void watch_destroy(gpointer data)
{
	struct SourceWatchData *watchData = static_cast<struct SourceWatchData*>(data);
	if (watchData->notifyfd >= 0 && watchData->watchfd >= 0) {
		inotify_rm_watch(watchData->notifyfd, watchData->watchfd);
	}
    g_free(watchData);
}


/**
 * Initialize this plugin dir watchData.
 *
 * @return true if successful, false if not.
 */
bool PluginDirWatcher::init(const char* path)
{
	assert(path != NULL);
    if (mPath) {
        free(mPath);
    }
	mPath = strdup(path);
	
    g_debug("Adding plugin watchData for '%s'", mPath);

	struct SourceWatchData *watchData = g_new0(struct SourceWatchData, 1);
	watchData->notifyfd = -1;
	watchData->watchfd = -1;

	watchData->notifyfd = inotify_init();
	if (watchData->notifyfd < 0) {
		g_warning("Error initializing inotify");
		return false;
	}

    watchData->watchfd = inotify_add_watch(watchData->notifyfd, mPath,
					IN_CREATE	|
					IN_DELETE	|
					IN_DELETE_SELF	|
					IN_MOVED_FROM	|
					IN_MOVED_TO	|
					IN_CLOSE_WRITE	|
					IN_ONLYDIR	|
					0);
	if (watchData->watchfd < 0) {
		perror("Can't call inotify_add_watchData");
		return false;
	}

    GIOChannel* chan = g_io_channel_unix_new(watchData->notifyfd);

    mSource = g_io_create_watch(chan, G_IO_IN);
    g_source_set_callback(mSource, reinterpret_cast<GSourceFunc>(watch_func), watchData, watch_destroy);

	GMainLoop* pLoop = BrowserServer::instance()->mainLoop();
    g_source_attach (mSource, g_main_loop_get_context(pLoop));

    g_io_channel_unref(chan);

	return true;
}

PluginDirWatcher::PluginDirWatcher()
{
	mSource = NULL;
	mPath = NULL;
}

PluginDirWatcher::~PluginDirWatcher()
{
	free(mPath);
	if (mSource) {
		g_source_destroy(mSource);
	}
}

void PluginDirWatcher::suspend()
{
	g_source_destroy(mSource);
	mSource = NULL;
}

void PluginDirWatcher::resume()
{
    if (mPath) {
        char *path = strdup(mPath);
        init(path);
        free(path);
    }
}
