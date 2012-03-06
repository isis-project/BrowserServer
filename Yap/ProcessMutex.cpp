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

#include <pthread.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/time.h>

#include "ProcessMutex.h"

static const uint32_t s_marker = 0xBAADF00D;

struct Header {
    uint32_t         marker1;
    pthread_mutex_t  mutex;
    uint32_t         marker2;
};

ProcessMutex::ProcessMutex(int size, int key)
    : m_key(key)
    , m_data(0)
    , m_dataSize(sizeof(Header) + size)
{
    if (key < 0) {

        while (m_key < 0) {
            struct timeval tv;
            gettimeofday(&tv, NULL);
            key_t ipckey = ftok(".", tv.tv_usec);
            m_key = ::shmget(ipckey, m_dataSize, 0644 | IPC_CREAT | IPC_EXCL);
            if (m_key == -1 && errno != EEXIST) {
                fprintf(stderr, "ProcessMutex: failed to create mutex: %s\n", strerror(errno));
                return;
            }
        }

        m_data = ::shmat(m_key, NULL, 0);
        if (-1 == (int)m_data) {
            fprintf(stderr, "ERROR %d attaching to shared memory key %d: %s\n", errno, m_key, strerror(errno));
            m_data = 0;
            return;
        }

        // Auto delete when all processes detach
        ::shmctl(m_key, IPC_RMID, NULL);

        Header* header = (Header*) m_data;
        header->marker1 = s_marker;
        header->marker2 = s_marker;

        pthread_mutexattr_t attr;
        pthread_mutexattr_init (&attr);
        pthread_mutexattr_setpshared (&attr, PTHREAD_PROCESS_SHARED);

        pthread_mutex_t* mutex = (pthread_mutex_t*) &header->mutex;
        pthread_mutex_init (mutex, &attr);
        pthread_mutexattr_destroy(&attr);
    }
    else {

        m_data = ::shmat(m_key, NULL, 0);
        if (m_data == (void*) -1) {
            fprintf(stderr, "Failed to attach to shmid: %d, ERROR: %d, %s", m_key, errno, strerror(errno));
            m_data = 0;
            return;
        }

        if (!isValid()) {
            fprintf(stderr, "Shared buffer is corrupted\n");
            ::shmdt(m_data);
            m_data = 0;
            return;
	}
    }
}

ProcessMutex::~ProcessMutex()
{
    if (m_data)
        ::shmdt(m_data);
}

bool ProcessMutex::tryLock(int numTries)
{
    Header* header = (Header*) m_data;

    while (numTries > 0) {
        if (pthread_mutex_trylock(&header->mutex) == 0)
            return true;

        // FIXME: Will this really yield to a lower priority process?
        (void)::sched_yield();

        numTries--;
    }

    return false;
}

void ProcessMutex::lock()
{
    Header* header = (Header*) m_data;
    pthread_mutex_lock(&header->mutex);
}

void ProcessMutex::unlock()
{
    Header* header = (Header*) m_data;

    pthread_mutex_unlock(&header->mutex);
}

void* ProcessMutex::data() const
{
    if (!m_data)
        return 0;

    return ((uint8_t*) m_data + sizeof(Header));
}

bool ProcessMutex::isValid() const
{
    if (!m_data)
        return false;

    Header* header = (Header*) m_data;
    if (header->marker1 != s_marker ||
        header->marker2 != s_marker)
        return false;

    return true;
}
