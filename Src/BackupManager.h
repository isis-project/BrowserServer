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

#ifndef BACKUP_MANAGER_H
#define BACKUP_MANAGER_H

#include <string>
#include <list>
#include <map>
#include "lunaservice.h"
#include "DbBackupStatus.h"
#include "JsonUtils.h"

struct LSHandle;
struct LSMessage;

const char* const k_PhonyCookieUrl = "cookie:db";

/**
 * Manages the backup and restore of the Mojo applications (files and HTML5 databases)
 * for the system.
 */
class BackupManager
{
public:

	bool	init			(GMainLoop* mainLoop, LSHandle* server_handle);
	void	dbDumpStarted	(const DbBackupStatus& status, void* userData);
	void	dbDumpStopped	(const DbBackupStatus& status, void* userData);
	void	dbRestoreStarted(const DbBackupStatus& status, void* userData);
	void	dbRestoreStopped(const DbBackupStatus& status, void* userData);

    static BackupManager* instance();
    void testDb();

private:

    /**
     * The different types of things that the backup service can backup/restore.
     */
    enum BackupType {
        BackupFile = 0,      // This number is defined by the backup service API.
        BackupDirectory = 1, // This number is defined by the backup service API.
        BackupHtml5Db = 100,
    };

    /**
     * A description of an item that the BackupManager is managing the backup and restoration of.
     */
    struct BackupItem {
        std::string m_appid;     ///< The app id whose data we're managing the backup of.
        std::string m_path;      ///< The path to the file/directory we're backing up.
        std::string m_dbname;    ///< HTML5 database name - only for type BackupHtml5Db.
        BackupType  m_eType;     ///< The type of thing we're backing up.
        int m_recursive;         ///< For dir backups only.
        std::string m_metaData;  ///< Our own metadata we keep for a backup item.
        double m_version;        ///< The version of the backup item.
        time_t m_dbModTime;      ///< The last time the database file was modified.

        BackupItem() : m_eType(BackupFile), m_recursive(0), m_version(0.0), m_dbModTime(0) {}
        BackupItem(const std::string& appId, BackupType eType, int recursive, double version);
        BackupItem(const BackupItem& src);
        BackupItem& operator=(const BackupItem& rhs);
        void setMetadata();
    };

    struct BackupOperationData {
        BackupManager* mgr;
        LSMessage* requestMessage;
        BackupItem item; ///< The item being backed up or restored
    };

    BackupManager();
    ~BackupManager();

    bool registerForBackup();
    bool queryBackupService();
    bool registerWithServiceBus();
    bool unregisterWithServiceBus();
    bool registerBackupServiceMethods();
    bool registerForBackupServiceStatus();
    static bool preBackup(LSHandle* lshandle, LSMessage *message, void *user_data);
    static bool postBackup(LSHandle* lshandle, LSMessage *message, void *user_data);
    static bool preRestore(LSHandle* lshandle, LSMessage *message, void *user_data);
    static bool postRestore(LSHandle* lshandle, LSMessage *message, void *user_data);
    bool sendItemMessageReply(LSMessage *message, const std::string& operation,
    const BackupItem& item, const std::string& errorText);
    bool sendEmptyResponse(LSMessage *message);
    bool sendPreBackupReply(LSMessage *message,const char* url,const std::string& errorText);

    static bool backupRegistrationCallback(LSHandle *sh, LSMessage *message, void *ctx);
    static bool backupCallback( LSHandle* lshandle, LSMessage *message, void *user_data);
    static bool queryBackupServiceCallback(LSHandle *sh, LSMessage *message, void *ctx);
    static bool parseBackupServiceItem(pbnjson::JValue& jsonItem, BackupItem& item);
    static int  parseBackupServiceItemList(pbnjson::JValue& jsonItems, std::map<std::string, BackupItem>& items);
    static std::string getHtml5BackupFile(const std::string& appid,const std::string& path);
    static std::string getHtml5Url(const std::string& appid);
    static bool simpleMessageReply(LSHandle* lshandle, LSMessage *message, const std::string& errorText);
    static void addItemDataToJsonObj(pbnjson::JValue& obj, const BackupItem& item);
    static bool backupServiceConnectCallback(LSHandle *sh, LSMessage *message, void *ctx);

    GMainLoop* m_mainLoop;
    LSHandle* m_clientService; ///< The client's connection to the backup service.
    LSPalmService* m_serverService; ///< The methods we expose to the backup service.
    LSMessageToken m_backupServiceStatusToken; ///< Used to know when backup services goes up/down.
    BackupItem m_backupItem; ///< Backing up browser cookies
    std::map<std::string, time_t> m_currentBackupModTimes;
    static LSMethod s_BackupServerMethods[];
    static BackupManager* s_instance;

};


#endif // BACKUP_MANAGER_H
