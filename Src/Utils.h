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

#ifndef BS_UTILS_H
#define BS_UTILS_H

#undef min
#undef max
#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <QString>

#include <openssl/x509.h>

char* readFile(const char* filePath);
bool  writeFile(const char* filePath, const char* buffer);
bool  deleteFile(const char* filePath);

template <class T>
std::string toSTLString(const T &arg) {
	std::ostringstream	out;
	out << arg;
	return(out.str());
}

/*
 * Given a candidate certificate "cert", find a match in the local certificate store
 * alternately, find a local match for a certificate in the file "certFileAndPath"
 * 
 * returns the local certificate in X509 form if found, NULL otherwise
 * if non-null return, then 'ret_certSerialNb' is populated with the serial number
 * (which can be used to mess with the cert in the PmCertManager)
 * 
 * IMPORTANT: X509_free() must be called by the caller on the X509 objects returned
 */
X509 * 	findSSLCertInLocalStore(X509 * cert,int& ret_certSerialNb);
X509 *	findSSLCertInLocalStore(const char * certFileAndPath,int& ret_certSerialNb);

/**
 * Return a file name such that it will be uniquely named
 * and won't collide with another file in the same dir.
 * @note The directory must exist for this file.
 */
QString makeUniqueFileName(const QString &inFileName);

#endif /* UTILS_H */
