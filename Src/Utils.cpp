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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <glib.h>

#include "Utils.h"
#include <openssl/x509_vfy.h>
#include <openssl/ssl.h>
#include <QFileInfo>

#include <cert_mgr.h>
#include <cert_mgr_prv.h>
#include <cert_x509.h>
#ifdef __cplusplus
extern "C" {
#endif
#include <cert_db.h>
#ifdef __cplusplus
}
#endif

char* readFile(const char* filePath)
{
    if (!filePath)
        return 0;

    FILE* f = fopen(filePath,"r");

    if (!f)
        return 0;

    fseek(f, 0L, SEEK_END);
    long sz = ftell(f);
    fseek( f, 0L, SEEK_SET );
    if (!sz) {
        fclose(f);
        return 0;
    }

    char* ptr = new char[sz+1];
    if( !ptr )
    {
        fclose(f);
        return 0;
    }
    ptr[sz] = 0;

    if (fread(ptr, sz, 1, f) != size_t(sz)) {
        delete [] ptr;
        ptr = NULL;
    }
    fclose(f);

    return ptr;
}


bool writeFile(const char* filePath, const char* buffer)
{
    if (!filePath || !buffer)
        return 0;

    FILE* f = fopen(filePath, "w");
    if (!f)
        return false;

    int len = strlen(buffer) + 1;
    int written = fwrite(buffer, 1, len, f);
    if (written != len) {
        fclose(f);
        unlink(filePath);
        return false;
    }

    fclose(f);
    return true;
}

bool deleteFile(const char* filePath)
{
    if (!filePath)
        return false;

    return (unlink(filePath) == 0);


X509 * findSSLCertInLocalStore(X509 * cert,int& ret_certSerialNb)
{
    if (cert == NULL)
        return NULL;

    int items=0;
    SSL_library_init();
    SSL_load_error_strings();
    CertReturnCode_t result = CertGetDatabaseInfo(CERT_DATABASE_SIZE, &items);
    //printf("database size %d %d ", result, items);
    if (result == CERT_OK) {
        for (int i = 0; i < items; i++) {
            char serialStr[128];
            result = CertGetDatabaseStrValue(i, CERT_DATABASE_ITEM_SERIAL, serialStr, 128);
            //printf("list id %d %d %s ", i, result, serialStr);
            if (CERT_OK == result) {
                char dir[MAX_CERT_PATH];
                char * endPtr = NULL;
                int serial = strtol(serialStr, &endPtr, 16);
                result = makePathToCert(serial, dir, MAX_CERT_PATH);
                //printf("list dir %d %s ", result, dir);
                if (CERT_OK == result) {
                    X509 *candidate_cert = NULL;
                    char buf[128] = { '\0' };
                    memset(buf, 0, sizeof(buf));
                    result = CertPemToX509(dir, &candidate_cert);
                    if (candidate_cert == NULL)
                        continue;
                    //printf("list cert %d ", result);
                    if (result == CERT_OK) {
                        //DO COMPARISON
                        if (X509_cmp(candidate_cert,cert) == 0) {
                            ret_certSerialNb = serial;
                            return candidate_cert;
                        }
                    }
                }
            }
        }
    }
    return NULL;
}

X509 * findSSLCertInLocalStore(const char * certFileAndPath,int& ret_certSerialNb)
{

    FILE * fp = fopen(certFileAndPath,"rb");
    if (fp == NULL) {
        return NULL;
    }
    X509 *cert = PEM_read_X509(fp, NULL, 0, NULL);
    if (cert == NULL)
    {
        return NULL;
    }

    X509 * inStoreCert = findSSLCertInLocalStore(cert,ret_certSerialNb);
    X509_free(cert);
    return inStoreCert;
}

QString makeUniqueFileName(const QString &inFileName)
{
    if (!QFile::exists(inFileName))
        return inFileName;

    QFileInfo fi (inFileName);
    const QString path = fi.absolutePath();
    const QString base = fi.completeBaseName();
    const QString suffix = fi.suffix();
    QString fileName;

    for (int ix = 1; ix < 200; ix++) {
        fileName = QString("%1/%2(%3).%4").arg(path).arg(base).arg(ix).arg(suffix);
        if(!QFile::exists(fileName))
            return fileName;
    }

    return fileName;
}
