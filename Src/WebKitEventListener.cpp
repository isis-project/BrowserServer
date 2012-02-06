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
#include "WebKitEventListener.h"
#include "BackupManager.h"


WebKitEventListener::WebKitEventListener(BackupManager* backupMgr) :
    m_backupMgr(backupMgr)
{
    assert(backupMgr != NULL);
}

void WebKitEventListener::dbDumpStarted( const DbBackupStatus& status, void* userData )
{
    m_backupMgr->dbDumpStarted(status, userData);
}

void WebKitEventListener::dbDumpStopped( const DbBackupStatus& status, void* userData )
{
    m_backupMgr->dbDumpStopped(status, userData);
}

void WebKitEventListener::dbRestoreStarted( const DbBackupStatus& status, void* userData )
{
    m_backupMgr->dbRestoreStarted(status, userData);
}

void WebKitEventListener::dbRestoreStopped( const DbBackupStatus& status, void* userData )
{
    m_backupMgr->dbRestoreStopped(status, userData);
}

void WebKitEventListener::dbMoveStatus( int err )
{
    if (err == 0) {
        g_message("Successfully moved HTML5 databases");
    }
    else {
        g_warning("ERROR %d moving HTML5 databases", err);
    }
}
