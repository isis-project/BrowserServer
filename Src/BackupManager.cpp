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

#include <syslog.h>
#include <assert.h>
#include <stdlib.h>
#include <string>
#include <map>
#include "BackupManager.h"
#include "JsonUtils.h"

/*
 * This file is almost identical to one in luna-sysmgr. If you change this please
 * compare with the one in luna-sysmgr to see if it also needs the same change.
 */


/**
 * The name that we use when identifying ourselves to the backup service.
 */
static const std::string strBackupServiceName("com.palm.browserServer");

static const std::string strPreBackupFunc("backup/preBackup");
static const std::string strPostBackupFunc("backup/postBackup");
static const std::string strPreRestoreFunc("backup/preRestore");
static const std::string strPostRestoreFunc("backup/postRestore");
BackupManager* BackupManager::s_instance = NULL;

/**
 * We use the same API for backing up HTML5 databases as we do the cookie
 * database. This is a phony appid that we use to identify the cookie db entry.
 */
static const std::string strPhonyCookieAppId = "com.palm.browserServer.cookies";

/**
 * Webkit now creates cookie databases by appID, so we need to make sure
 * that we backup/restore the one created by the browser app
 */
static const std::string strCookieDbAppId = "com.palm.app.browser";

/**
 * These are the methods that the backup service can call when it's doing a
 * backup or restore.
 */
LSMethod BackupManager::s_BackupServerMethods[]  = {
    { "preBackup"  , BackupManager::preBackup },
    { "postBackup" , BackupManager::postBackup },
    { "preRestore" , BackupManager::preRestore },
    { "postRestore", BackupManager::postRestore },
    { 0, 0 }
};


BackupManager::BackupItem::BackupItem(const std::string& appId, BackupType eType, int recursive, double version)
    : m_appid(appId)
    , m_eType(eType)
    , m_recursive(recursive)
    , m_version(version)
    , m_dbModTime(0)
{
    setMetadata();
}

BackupManager::BackupItem::BackupItem(const BackupItem& src)
    : m_appid(src.m_appid)
    , m_path(src.m_path)
    , m_dbname(src.m_dbname)
    , m_eType(src.m_eType)
    , m_recursive(src.m_recursive)
    , m_metaData(src.m_metaData)
    , m_version(src.m_version)
    , m_dbModTime(src.m_dbModTime)
{
    setMetadata();
}

BackupManager::BackupItem& BackupManager::BackupItem::operator=(const BackupItem& rhs)
{
    m_appid     = rhs.m_appid;
    m_path      = rhs.m_path;
    m_dbname    = rhs.m_dbname;
    m_eType     = rhs.m_eType;
    m_recursive = rhs.m_recursive;
    m_metaData  = rhs.m_metaData;
    m_version   = rhs.m_version;
    m_dbModTime = rhs.m_dbModTime;

    setMetadata();

    return *this;
}

void BackupManager::BackupItem::setMetadata()
{
    pbnjson::JValue metadata = pbnjson::Object();
    metadata.put("appId", m_appid);
    metadata.put("myType", (int) m_eType);
    if (!m_dbname.empty())
        metadata.put("dbName", m_dbname);
    metadata.put("dbModTime", (int) m_dbModTime);

    (void) jValueToJsonString(m_metaData, metadata);
}

BackupManager::BackupManager()
    : m_mainLoop(0)
    , m_clientService(0)
    , m_serverService(0)
    , m_backupServiceStatusToken(0)
{
    BackupItem lunaSysMgrCookies(strPhonyCookieAppId, BackupHtml5Db, 0 /*recursive*/, 1.0);
    lunaSysMgrCookies.m_dbname = "cookies"; // ignored
    lunaSysMgrCookies.setMetadata();

    m_backupItem=lunaSysMgrCookies;
}

BackupManager::~BackupManager()
{

}

BackupManager* BackupManager::instance()
{
    if (NULL == s_instance) {
        s_instance = new BackupManager();
    }

    return s_instance;
}

/**
 * The temp sql backup dump file we create when backing up a HTML5 database.
 */
std::string BackupManager::getHtml5BackupFile(const std::string& appId,const std::string& path)
{
    // Don't change this location without coordinating with the backup service.
    // This is because the backup service restores files to this location before
    // calling restore.
    return std::string(path + appId + "-html5-backup.sql");
}

/**
 * Calculate the URL used for a Mojo application's HTML5 database.
 */
std::string BackupManager::getHtml5Url(const std::string& appId)
{
    if (appId == strPhonyCookieAppId) {
        return k_PhonyCookieUrl;
    }
    else {
        assert(false);
        return "";
    }
}

void BackupManager::dbDumpStarted( const DbBackupStatus& status, void* userData )
{
    //this is called by webkit db dump method when it starts dumping the db
    BackupOperationData* opdata = static_cast<BackupOperationData*>(userData);
    assert(opdata != NULL);
    g_message("Started dump of %s to %s, err: %d", status.url.c_str(), opdata->item.m_path.c_str(), status.err);
}

void BackupManager::dbDumpStopped( const DbBackupStatus& status, void* userData )
{
    //entered here after webkit finishes dumping the db for us to respond to pre backup
    BackupOperationData* opdata = static_cast<BackupOperationData*>(userData);
    assert(opdata != NULL);

    g_message("Stopped dump of %s to %s, err: %d", status.url.c_str(), opdata->item.m_path.c_str(), status.err);
    std::string msg;
    if (status.err) {
        char* tmp(NULL);
        if (asprintf(&tmp, "ERROR %d dumping database", status.err) >= 0) {
            msg = tmp;
            free(tmp);
        }
    }

    sendPreBackupReply(opdata->requestMessage,std::string( opdata->item.m_appid+"-html5-backup.sql").c_str(),msg);
    delete opdata;
}

void BackupManager::dbRestoreStarted( const DbBackupStatus& status, void* userData )
{
    g_message("Started restore of %s", status.url.c_str());
}

void BackupManager::dbRestoreStopped( const DbBackupStatus& status, void* userData )
{
    BackupOperationData* opdata = static_cast<BackupOperationData*>(userData);
    assert(opdata != NULL);

    g_message("Stopped restore of %s", status.url.c_str());
    std::string msg;
    if (status.err) {
        char* tmp(NULL);
        if (asprintf(&tmp, "ERROR %d restoring database", status.err) >= 0) {
            msg = tmp;
            free(tmp);
        }
    }
    else {
        assert(access(opdata->item.m_path.c_str(), R_OK) == 0);
    }
    //we send back the response to backup service after restore is completed
    s_instance->sendEmptyResponse(opdata->requestMessage);

    ::unlink(opdata->item.m_path.c_str());

    delete opdata;
}

bool BackupManager::preBackup( LSHandle* lshandle, LSMessage *message, void *user_data )
{
    g_warning("In preBackup");

    std::string msg;

    const char* str = LSMessageGetPayload( message );
    pbnjson::JValue args;
    (void) jsonStringToJValue(args, str);
    if (!str) {
        msg = "No payload";
    }
    else {
        g_debug("preBackup '%s'", str);
    }

    BackupItem item =s_instance->m_backupItem;

    time_t  modTime(0);
    int     numOpenMods(0);

    //s_instance->m_currentBackupModTimes.erase(item.m_path);
    std::string url = getHtml5Url(strPhonyCookieAppId);
    std::string backupPath = args["tempDir"].asString();
    item.m_path=getHtml5BackupFile(item.m_appid,backupPath);
    s_instance->m_backupItem.m_path =item.m_path;

    g_message("Backing up HTML5 database for %s to %s.  modTime: %lu, mods:%d",
            item.m_appid.c_str(), item.m_path.c_str(), modTime, numOpenMods);
    BackupOperationData* opdata = new BackupOperationData();
    opdata->requestMessage = message;
    opdata->mgr = s_instance;
    opdata->item = item;

    bool succeeded = false;

    if (succeeded) {
        g_debug("Database dump has started.");
        // Remember this db's modification time so that when backup is complete
        // I can save it so that subsequent backups can look for changes.
        //not sure if we still need this as we don't do incremental backup
        //s_instance->m_currentBackupModTimes[item.m_path] = modTime;

    }
    else {
        g_warning("Database dump not successful!");
        //just send an empty response
        s_instance->sendEmptyResponse(message);
    }


    return true;
}

bool BackupManager::postBackup( LSHandle* lshandle, LSMessage *message, void *user_data)
{
    g_warning("In postbackup");
    //we dont do much here as we dont need to do anything
    //but will have to respond with an empty response object

    return s_instance->sendEmptyResponse(message);
}

bool BackupManager::preRestore(  LSHandle* lshandle, LSMessage *message, void *user_data)
{
    // Need to respond to continue with the restore
    g_warning("In preRestore");
    pbnjson::JValue response = pbnjson::Object();

    LSError lserror;
    LSErrorInit(&lserror);

    response.put("proceed", true);

    std::string responseStr;
    if (jValueToJsonString(responseStr, response)) {
        g_debug("Sending reply: for preRestore");
        if (!LSMessageReply( s_instance->m_clientService, message, responseStr.c_str(), &lserror )) {
            g_warning("Can't send error reply %s", lserror.message);
            LSErrorFree (&lserror);
        }
    }
    else {
        g_warning("Proper preRestore response not sent");
        s_instance->sendEmptyResponse(message);
    }
    return true;
}


bool BackupManager::postRestore(  LSHandle* lshandle, LSMessage *message, void *user_data)
{
    g_warning("In postRestore");
    const char* str = LSMessageGetPayload( message );

    bool sendEmptyResponse=true;
    if (!str) {
        g_debug("No response");
    }
    else {
        g_debug("postRestore '%s'", str);
        pbnjson::JValue args;
        (void) lsMessageToJValue(args, message);
        std::string file = args["files"][0].asString();
        //first check if the file has the absolute path, if yes no need to check for tempDir
        std::string path;
        if(!file.empty())
        {
            if(file.at(0)=='/')
            {
                //it is an absolute path so look for it as is
                path=file;
            }
            else
            {
                //just the file name so construct path using the tempDir sent
                path = args["tempDir"].asString() + file;
            }


            BackupItem item=s_instance->m_backupItem;
            if (item.m_eType == BackupHtml5Db) {
                g_message("Restoring HTML5 database for %s from %s", item.m_appid.c_str(), path.c_str());
                if (0 == ::access(path.c_str(), R_OK)) {
                    BackupOperationData* opdata = new BackupOperationData();
                    opdata->requestMessage = message;
                    opdata->mgr = s_instance;
                    opdata->item = item;

                    bool succeeded = false;

                    if (!succeeded) {
                        delete opdata;
                        g_warning("Database restore failed");
                    }else{
                        //we will send the empty response after restore is completed
                        sendEmptyResponse=false;
                    }
                }
                else
                {
                    g_warning("Path Incorrect!");

                }
            }
            else
            {
                // Nothing to do
                g_warning("Db Not BackupHtml5Db type");

            }
        }
    }
    if(sendEmptyResponse)
        return s_instance->sendEmptyResponse(message);

    return true;
}

bool BackupManager::sendEmptyResponse(LSMessage *message)
{
    pbnjson::JValue response = pbnjson::Object();
    std::string responseStr;
    if (jValueToJsonString(responseStr, response)) {
        LSError lserror;
        LSErrorInit(&lserror);

        g_debug("Sending empty reply: ");
        if (!LSMessageReply( s_instance->m_clientService, message, responseStr.c_str(), &lserror )) {
            g_warning("Can't send error reply %s", lserror.message);
            LSErrorFree (&lserror);
        }
    }
    else {
        g_warning("Proper response not sent");
        return false;
    }
    return true;
}

bool BackupManager::sendPreBackupReply(LSMessage *message,const char* url,const std::string& errorText)
{
    g_warning("sendPreBackupReply");
    pbnjson::JValue response = pbnjson::Object();

    LSError lserror;
    LSErrorInit(&lserror);

    pbnjson::JValue urlList = pbnjson::Array();
    urlList.append(url);
    response.put("description", "Backing up Cookies DB");
    response.put("files", urlList);
    response.put("full", true);
    response.put("version", "1.0");
    if (!errorText.empty())
        response.put("errorText", errorText);

    std::string responseStr;
    if (jValueToJsonString(responseStr, response)) {
        g_debug("Sending reply: for PreBackup");
        if (!LSMessageReply( m_clientService, message, responseStr.c_str(), &lserror )) {
            g_warning("Can't send error reply %s", lserror.message);
            LSErrorFree (&lserror);
        }
        return true;
    } else {
        return false;
    }
}

/**
 * Initialize the backup manager.
 */
bool BackupManager::init(GMainLoop* mainLoop, LSHandle* handle)
{
    assert(m_mainLoop == NULL); // Only initialize once.
    m_mainLoop = mainLoop;
    bool succeeded=true;

    m_clientService = handle;
    if (NULL == m_clientService) {
        g_warning("unable to get private handle");
        succeeded = false;
    }

    if (succeeded) {
        succeeded = registerBackupServiceMethods();
    }

    // Now we are doing static backup registration
    //if (succeeded) {
    //    succeeded = registerForBackup();
    //}

    g_debug("Initialized backup manager success: %c", succeeded ? 'Y' : 'N');

    return succeeded;
}

/**
 * The callback that receives the response when registering for to backup an item.
 */
bool BackupManager::backupRegistrationCallback(LSHandle *sh, LSMessage *message, void *ctx)
{
    g_warning("backupRegistrationCallback.");
    if (!message)
        return true;

    const char* payload = LSMessageGetPayload(message);
    if (!payload)
        return true;

    pbnjson::JValue args;
    if (!jsonStringToJValue(args, payload)) {
        g_warning("Failed registering for backup service.");
        return false;
    }
    return true;
}

/**
 * Register to backup a single item with the backup service.
 */
bool BackupManager::registerForBackup()
{
    g_warning("Requesting registration for backup");
    pbnjson::JValue payload = pbnjson::Object();

    payload.put("service", strBackupServiceName);
    payload.put("preBackup", strPreBackupFunc);
    payload.put("postBackup", strPostBackupFunc);
    payload.put("preRestore", strPreRestoreFunc);
    payload.put("postRestore", strPostRestoreFunc);

    LSError error;
    LSErrorInit(&error);

    bool succeeded = false;
    std::string payloadStr;
    if (jValueToJsonString(payloadStr, payload)) {
        succeeded = LSCall(m_clientService, "palm://com.palm.service.backup/register", payloadStr.c_str(),
                           backupRegistrationCallback, NULL, NULL, &error);
        if (!succeeded) {
            g_warning("Failed registering for backup: %s", error.message);
            LSErrorFree(&error);
        }
    }

    return succeeded;
}

/**
 * Parse an array of items into a list.
 *
 * @return The number of items that had errors.
 */
int BackupManager::parseBackupServiceItemList(pbnjson::JValue& jsonItems, std::map<std::string, BackupItem>& items)
{
    int nNumErrors = 0;

    if (jsonItems.isArray()) {
        int numItems = jsonItems.arraySize();
        for (int i = 0; i < numItems; ++i) {
            pbnjson::JValue backupItem = jsonItems[i];
            if (backupItem.isObject()) {
                std::string serviceName = backupItem["service"].asString();
                if (serviceName.empty() || serviceName == strBackupServiceName) {
                    BackupItem item;
                    if (parseBackupServiceItem(backupItem, item)) {
                        items[item.m_path] = item;
                    }
                    else {
                        nNumErrors++;
                    }
                }
            }
            else {
                g_warning("Invalid JSON");
                nNumErrors++;
            }
        }
    }
    else {
        nNumErrors = 999;
    }

    return nNumErrors;
}

bool BackupManager::parseBackupServiceItem(pbnjson::JValue& jsonItem, BackupItem& item)
{
    item.m_path = jsonItem["path"].asString();
    item.m_version = jsonItem["version"].asNumber<double>();
    int nType(0);
    nType = jsonItem["type"].asNumber<int>();
    if (nType == 0) {
        item.m_eType = BackupFile;
    }
    else if (nType == 1) {
        item.m_eType = BackupFile;
    }
    else {
        assert(false);
        return false;
    }

    item.m_recursive = jsonItem["recursive"].asNumber<int>();
    item.m_metaData = jsonItem["metadata"].asString();
    if (!item.m_metaData.empty()) {
        pbnjson::JValue metadata;
        if (jsonStringToJValue(metadata, item.m_metaData)) {
            item.m_appid = metadata["appId"].asString();
            item.m_dbname = metadata["dbName"].asString();
            int myType;
            if (metadata["myType"].isNumber()) {
                myType = metadata["myType"].asNumber<int>();
                item.m_eType = static_cast<BackupType>(myType);
            }
            double modTime;
            if (metadata["dbModTime"].isNumber()) {
                modTime = metadata["dbModTime"].asNumber<double>();
                item.m_dbModTime = static_cast<time_t>(modTime);
            }

            if (item.m_eType == BackupHtml5Db && item.m_dbname.empty()) {
                g_warning("HTML5 db's need a database name.");
                return false;
            }
        }
        else {
            g_warning("Can't parse item metadata");
            return false;
        }
    }
    else {
        g_warning("Can't find item metadata");
        return false;
    }

    return true;
}

/**
 * Register the calls that the backup service will call when it does a backup or
 * restore.
 */
bool BackupManager::registerBackupServiceMethods()
{
    g_warning("Registering methods");
    assert(NULL != m_mainLoop);
    //assert(NULL != m_serverService);

    LSError error;
    LSErrorInit(&error);

    bool succeeded = LSRegisterCategory(m_clientService, "backup", s_BackupServerMethods, NULL, NULL, &error);

    return succeeded;
}
