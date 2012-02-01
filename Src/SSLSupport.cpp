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

#include <glib.h>
#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "SSLSupport.h"

//static initializers here
pthread_mutex_t SSLSupport::s_initLock = PTHREAD_MUTEX_INITIALIZER;
int SSLSupport::s_initCounter = 0;
pthread_mutex_t * SSLSupport::s_mutexArray = NULL;
long SSLSupport::s_nLocks = 0;

unsigned long SSLSupport::getThreadID(void)
{
	return ((unsigned long) pthread_self());
}

void SSLSupport::lock(int lockingMode, int lockNumber, const char *,int)
{
	if (lockingMode & CRYPTO_LOCK) {
		pthread_mutex_lock(&s_mutexArray[lockNumber]);
	} else {
		pthread_mutex_unlock(&s_mutexArray[lockNumber]);
	}
}

void SSLSupport::init(void)
{
	pthread_mutex_lock(&s_initLock);
	if (s_initCounter) {
		//already inited, exit
		pthread_mutex_unlock(&s_initLock);
		return;
	}
	++s_initCounter;
	
	SSL_load_error_strings();                /* readable error messages */
	SSL_library_init();                      /* initialize library */
	OpenSSL_add_all_ciphers();
	OpenSSL_add_all_digests();
	ERR_load_crypto_strings();
	s_nLocks = CRYPTO_num_locks();
	
	s_mutexArray = (pthread_mutex_t*) OPENSSL_malloc(s_nLocks * sizeof(pthread_mutex_t));

	for (int i = 0; i < s_nLocks; i++) 
	{
		pthread_mutex_init(&s_mutexArray[i], NULL);
	}

	CRYPTO_set_id_callback(getThreadID);
	CRYPTO_set_locking_callback(lock);
	
	pthread_mutex_unlock(&s_initLock);
}

void SSLSupport::deinit(void)
{
	pthread_mutex_lock(&s_initLock);
	
	if (s_initCounter == 0) {
		//already de-inited, exit
		pthread_mutex_unlock(&s_initLock);
		return;
	}
	--s_initCounter;
	CRYPTO_set_locking_callback(NULL);

	for (int i = 0; i < s_nLocks; i++) {
		pthread_mutex_destroy(&s_mutexArray[i]);
	}

	OPENSSL_free(s_mutexArray);
	CRYPTO_set_id_callback(NULL);
	
	pthread_mutex_unlock(&s_initLock);
}
