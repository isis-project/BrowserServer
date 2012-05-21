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

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <syslog.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <dirent.h>

#include "BrowserPage.h"
#include "BrowserServer.h"
#include "BrowserCommon.h"
#include "CpuAffinity.h"
#include "Settings.h"
#include "SSLSupport.h"

#include <QApplication>
#include <QSettings>


// Uncomment the following define of DEBUG_SEGFAULT to enable a signal handler
// that will allow this process to loop forever to give us a chance to attach
// gdb and do some debugging instead of core'ing.
//#define DEBUG_SEGFAULT

#define kDoInitialTiming 0

static bool g_useSysLog = false;

#if (kDoInitialTiming == 1)

static struct timeval gTvStart;

static gboolean
PrvInitialTimerCallback(gpointer pArg)
{
    struct timeval tv;

    ::gettimeofday(&tv, 0);

    FILE* pFile = ::fopen("/tmp/BrowserServerStartup.txt", "w");
    ::fprintf(pFile, "Timer callback: %ld us\n",
              ((tv.tv_sec - gTvStart.tv_sec) * 1000000 +
               (tv.tv_usec - gTvStart.tv_usec)));
    ::printf("Timer callback: %ld us\n",
             ((tv.tv_sec - gTvStart.tv_sec) * 1000000 +
              (tv.tv_usec - gTvStart.tv_usec)));
    ::fclose(pFile);
    return false;
}

#endif

static void
PrvSigTermHandler(int)
{
    // Don't call syslog here because is uses malloc/free which isn't reentrant.
    fprintf(stderr, "SIGTERM received. Shutting down...\n");
    exit(0);
}

static void logFilter(const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer unused_data)
{
    if (g_useSysLog) {
        int priority;

        switch (log_level & G_LOG_LEVEL_MASK) {
            case G_LOG_LEVEL_CRITICAL:
                priority = LOG_CRIT;
                break;
            case G_LOG_LEVEL_ERROR:
                priority = LOG_ERR;
                break;
            case G_LOG_LEVEL_WARNING:
                priority = LOG_WARNING;
                break;
            case G_LOG_LEVEL_MESSAGE:
                priority = LOG_NOTICE;
                break;
            case G_LOG_LEVEL_DEBUG:
                priority = LOG_DEBUG;
                break;
            case G_LOG_LEVEL_INFO:
            default:
                priority = LOG_INFO;
                break;
        }
        syslog(priority, "%s", message);
    }
    else {
        g_log_default_handler(log_domain, log_level, message, unused_data);
    }
}

#ifdef DEBUG_SEGFAULT
volatile bool stayInLoop = true;
static void crashHandler(int sig)
{
    fprintf(stdout, "BrowserServer: Caught signal %d\n", sig);

    FILE *file;
    file = fopen("/tmp/BrowserServer-signal-occurred", "w");
    if (file) {
        fprintf(file, "BrowserServer: Caught signal %d\n", sig);
        fclose(file);
    }

    // Infinite loop until we turn off stayInLoop using gdb:
    while (stayInLoop) {
    }
}
#endif // DEBUG_SEGFAULT

/**
 * Is this process running as a daemon (i.e. by upstart)?
 */
static bool
isDaemonized()
{
    // This is only true because our devices don't have HOME set and the 
    // desktop does. It would be nice to have a more accurate way of knowing
    // that this process is daemonized.
    return getenv("HOME") == NULL;
}

/*
* non system() way to do recursive chowns
*/

static int
Chown(char *path, int owner, int group, bool recurse)
{
    struct stat statbuf;
    int rc;

    rc = stat(path,&statbuf);
    if (rc == 0) {
        if (S_ISDIR(statbuf.st_mode) && recurse) {
            char newpath[PATH_MAX];
            DIR * dir;
            struct dirent* dent;
            // chown this dir first
            if (chown(path, owner, group) != 0)
                return -1;

            dir = opendir(path);
            // recurse into subdirs
            if (dir) {
                int pathlen = strlen(path);
                while((dent = readdir(dir)) != NULL) {
                    int dlen = strlen(dent->d_name);
                    // skip . and ..
                    if (!((dlen == 1 && dent->d_name[0] == '.') || (dlen == 2 && dent->d_name[0] == '.' && dent->d_name[1] == '.'))) {
                        memcpy(newpath,path,pathlen);
                        newpath[pathlen] = '/';
                        strncpy(&newpath[pathlen+1], dent->d_name,PATH_MAX - pathlen - 1);
                        rc = Chown(newpath,owner,group,recurse);
                    }
            }
            closedir(dir);
            }
        }
        else {
            // actually chown
            rc = chown(path, owner, group);
        }
    }
    return rc;
}


static void InitPrivileges() {
#define NOTROOT
#ifdef NOTROOT
#if defined(SETUP_WEBOS_DEVICE_DIRECTORIES)
    const int kProcessId = 1000; // user "luna"
    const int kGroupId = 1000;  // group "luna"
    const int kGroup2Id = 44;  // group "video"

    // Set rlimit of mlock memory for the GPU mapping, needed by html5 GLES based implementation
    struct rlimit rl = {RLIM_INFINITY, RLIM_INFINITY};
    setrlimit(RLIMIT_MEMLOCK, &rl);
    // Drop process groups so we are no longer running as root. This is for security reasons
    // as we will be running Flash in this process.

    // Fix up the file system. We have to handle the case where is exists (and is wrong) and the case
    // where it has not been created yet.
    ::mkdir( "/var/palm", 0777 );
    ::mkdir( "/var/palm/data", 0777 );
    char path[1024];

#ifdef FIXME_QT
    Chown(PalmBrowserSettings()->sharedClipboardFile,kProcessId,
          kGroupId,true);
#endif

    // needed for cookies and googlemaps data:
    Chown((char*)"/var/palm/data",kProcessId,kGroupId,false);


    // do both mkdir and chmod to deal with the dir not being
    // there, or being there and having wrong perms:
    ::mkdir("/var/luna/data/browser",S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IWGRP|S_IXGRP|S_IROTH|S_IXOTH);
    chmod("/var/luna/data/browser",S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IWGRP|S_IXGRP|S_IROTH|S_IXOTH);
    ::mkdir("/var/luna/data/browser/icons",755);
    Chown((char*)"/var/luna/data/browser/icons",kProcessId,kGroupId,true);
    Chown((char*)"/var/palm/data/browser-cookies.db",kProcessId,kGroupId,true);
    // don't let other read cookies:
    chmod("/var/palm/data/browser-cookies.db",S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IWGRP|S_IXGRP);

    // local storage
    Chown((char*)"/var/palm/data/localstorage",kProcessId,kGroupId,true);

    // DSP for flash
    // make sure it allows group rw:
    chmod("/dev/DspBridge",S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
    // and set the group to luna:
    Chown((char*)"/dev/DspBridge",0,kGroupId,true);

    // make sure it allows group rw:
    chmod("/dev/msm_vidc_dec",S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
    // and set the group to luna:
    Chown((char*)"/dev/msm_vidc_dec",0,kGroupId,true);

    chmod("/dev/pmem_adsp",S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
    Chown((char*)"/dev/pmem_adsp",0,kGroupId,true);

    // for omx
    chmod("/dev/pmem_smipool",S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
    Chown((char*)"/dev/pmem_smipool",0,kGroupId,true);

    // for 2D and 3D
    // FIXME: why need this to launch GLES app?
    chmod("/dev/console",S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
    Chown((char*)"/dev/console",0,kGroupId,true);

    chmod("/dev/kgsl-2d0",S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
    Chown((char*)"/dev/kgsl-2d0",0,kGroupId,true);

    chmod("/dev/kgsl-2d1",S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
    Chown((char*)"/dev/kgsl-2d1",0,kGroupId,true);

    chmod("/dev/kgsl-3d0",S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
    Chown((char*)"/dev/kgsl-3d0",0,kGroupId,true);

    // flash needs this:
    chmod("/tmp/pipcserver.sysmgr",S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
    Chown((char*)"/tmp/pipcserver.sysmgr",0,kGroupId,false);

    // cert store
    // Chown((char*)"/var/ssl",kProcessId,kGroupId,true);
    // I use system instead (below) to chmod g+w /var/ssl

    // browser page dbs:
    // I use system here to handle the wildcards
    // putting all the commands in one system() makes it faster.
    // we chmod /var/ssl to root.luna so we can write to it.
    ::snprintf( path, 1023, "/bin/chmod -R g+w /var/ssl; /bin/chown -R root.luna /var/ssl; /bin/chown luna:luna -R /var/palm/data/http_* /var/palm/data/https_*; /bin/chmod -R o-rwx  /var/palm/data/http_* /var/palm/data/https_*");
    ::system( path );

    gid_t groups[22];
    // check groups and add ours to them:
    {
        int i, n;
        fprintf(stderr,"groups: ");
        n = getgroups(20,groups);
        if (n == -1) {
            perror("getgroups");
        } else {
            groups[n++] = kGroupId;
            groups[n++] = kGroup2Id;
            setgroups(n,groups);
        }
        n = getgroups(22,groups);
        if (n == -1) {
            perror("getgroups");
        } else {
            for(i = 0; i < n; i++) {
                fprintf(stderr,"%d ",groups[i]);
            }
        }
        fprintf(stderr,"\n");

    }

    chdir( "/home/luna" );
#ifdef SETGID_BUG_FIXME
    // now we drop privs
    setgid( kGroupId );
    setuid( kProcessId );
#endif
#endif // defined(SETUP_WEBOS_DEVICE_DIRECTORIES)
#endif // NOTROOT
}


// Message Handler for qDebug, qWarning, qCritical and qFatal messages
void PrvMessageHandler(QtMsgType type, const char *msg) {
    switch(type)
    {
        case QtDebugMsg: 
            g_debug("%s", msg);
            break;
        case QtWarningMsg:
            g_warning("%s", msg);
            break;
        case QtCriticalMsg:
            g_critical("%s", msg);
            break;
        case QtFatalMsg:
            g_error("%s", msg);
            break;
        default:
            g_message("%s", msg);
            break;
    }
}


/*
*
*
*/

int
main(int argc, char *argv[])
{
#if defined(TARGET_DEVICE)
    if(!::getenv("QT_PLUGIN_PATH"))
        ::setenv("QT_PLUGIN_PATH", "/usr/plugins", 1);
    if(!::getenv("QT_QPA_PLATFORM"))
        ::setenv("QT_QPA_PLATFORM", "qbs", 1);
#endif

    // Register a new message handler for qDebug/qWarning/qCritical/qFatal
    qInstallMsgHandler(PrvMessageHandler);

    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName("palm");
    QCoreApplication::setOrganizationDomain("hp.com");
    QCoreApplication::setApplicationName("BrowserServer");

    InitSettings();

    QSettings settings;

    GError *gerror = NULL;
    int deadlockTimeoutMs = settings.value("DeadlockTimeoutMs", 15000).toInt();

    syslog(LOG_INFO, "Starting BrowserServer");

    qDebug("BrowserServer compiled against Qt %s, running on %s", QT_VERSION_STR, qVersion());

#if !GLIB_CHECK_VERSION(2, 32, 0)
    if (!g_thread_supported()) {
        g_thread_init(NULL);
    }
#endif

    int remoteInspectorPort = qMax(0, QString(::getenv("BROWSERSERVER_INSPECTOR_PORT")).toInt());
    if (!remoteInspectorPort)
        remoteInspectorPort = qMax(0, settings.value("RemoteInspectorPort", 0).toInt());

    static GOptionEntry optEntries[] = {
        {"deadlock-timeout", 'd', 0, G_OPTION_ARG_INT, &deadlockTimeoutMs, "deadlock timeout (ms) : -1 means disabled", "N"},
        {"inspector-port", 'i', 0, G_OPTION_ARG_INT, &remoteInspectorPort, "remote inspector port : zero means disabled", NULL},
        { NULL }
    };

    GOptionContext *optContext = NULL;
    optContext = g_option_context_new("- browser backend");
    g_option_context_add_main_entries(optContext, optEntries, NULL);

    if (!g_option_context_parse(optContext, &argc, &argv, &gerror)) {
        g_critical("Error processing commandline args: \"%s\"", gerror->message);
        g_error_free(gerror);
        exit(EXIT_FAILURE);
    }

    g_option_context_free(optContext);

    if (remoteInspectorPort > 0)
        qDebug("Web inspector port : %d", remoteInspectorPort);

    BrowserPage::setInspectorPort(remoteInspectorPort);

    g_useSysLog = isDaemonized();
    g_log_set_default_handler(logFilter, NULL);

#ifdef USE_LUNA_SERVICE
    // Tie BrowserServer to Processor 1. (DFISH-7961)
    setCpuAffinity(getpid(), 1);
#endif //USE_LUNA_SERVICE

    ::signal(SIGPIPE, SIG_IGN);
    ::signal(SIGTERM, PrvSigTermHandler);
#ifdef DEBUG_SEGFAULT
    ::signal(SIGSEGV, crashHandler);   /* 11:  segmentation violation */
#endif

#if (kDoInitialTiming == 1)
    ::gettimeofday(&gTvStart, 0);
#endif

    BrowserServer* server = BrowserServer::instance();
    if (!server) {
        BERR("Failed to get instance of server");
        return -1;
    }

    if (!(server->init())) {
        BERR("BrowserServer initialization failed");
        return -1;
    }

    bool serviceStarted = server->startService();
    if (!serviceStarted) {
        BERR("Failed to start luna service");
        return -1;
    }

#ifdef USE_CERT_MGR
    SSLSupport::init();
#endif

    server->InitMemWatcher();

#if defined(USE_MEMCHUTE)
    MemchuteWatcher* memWatch =
        MemchuteWatcherNew(BrowserServer::handleMemchuteNotification);
    if (memWatch != NULL) {
        MemchuteGmainAttach(memWatch, server->mainLoop());
    } else {
        g_warning("%s: Unable to create MemchuteWatcher", __FUNCTION__);
    }
#endif

    // NOTE: We need to initialize the privileges before we start the GMainLoop
    // below.  This is to workarounf a bug where setgid() and setuid()
    // deadlocks with pthread_create().
    // See: http://sources.redhat.com/bugzilla/show_bug.cgi?id=3270
    InitPrivileges();

#if (kDoInitialTiming == 1)
    GMainLoop* pLoop = server->mainLoop();
    GSource* pSource = g_idle_source_new();

    g_source_set_callback(pSource, (GSourceFunc) PrvInitialTimerCallback, NULL, NULL);
    g_source_attach(pSource, g_main_loop_get_context(pLoop));
    g_source_unref(pSource);
#endif

    server->run(deadlockTimeoutMs);

#if defined(USE_MEMCHUTE)
    if (memWatch != NULL) {
        MemchuteWatcherDestroy(memWatch);
    }
#endif

    server->stopService();

#ifdef USE_CERT_MGR
    SSLSupport::deinit();
#endif
    return 0;
}



