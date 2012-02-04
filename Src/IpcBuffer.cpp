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
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <time.h>

#include <glib.h>

#include "IpcBuffer.h"

#ifndef SHM_CACHE_WRITETHROUGH
#define SHM_CACHE_WRITETHROUGH   0200000 /* custom! */
#endif

IpcBuffer* IpcBuffer::create(int size)
{
    int key = -1;
    while (key < 0) {

        struct timeval tv;
        gettimeofday(&tv, NULL);
        // ftok has unspecified behavior if the lower 8-bits are 0
        if ((tv.tv_usec & 0xFF) == 0)
            tv.tv_usec++;
        key_t k = ftok(".", tv.tv_usec);
        key = ::shmget(k, size, 0666 | IPC_CREAT | IPC_EXCL);
        if (key == -1 && errno != EEXIST) {
            g_critical("Failed to create shared buffer: %s", strerror(errno));
            return 0;
        }
    }

    void* buffer = ::shmat(key, NULL, SHM_CACHE_WRITETHROUGH);
    if (-1 == (int) buffer) {
        g_critical("Failed to attach to shared memory key (1) %d: %s", key, strerror(errno));
        ::shmctl(key, IPC_RMID, NULL);
        return 0;
    }

    // Auto delete when all processes detach
    ::shmctl(key, IPC_RMID, NULL);

    IpcBuffer* b = new IpcBuffer(key, size);
    b->m_buffer = buffer;

    return b;
}

// browserserver side
IpcBuffer* IpcBuffer::attach(int key, int size)
{
    void* buffer = ::shmat(key, NULL, 0);
    if (-1 == (int) buffer) {
        g_critical("Failed to attach to shared memory key (2) %d: %s", key, strerror(errno));
        return false;
    }

    IpcBuffer* b = new IpcBuffer(key, size);
    b->m_buffer = buffer;

    return b;
}


IpcBuffer::IpcBuffer(int key, int size)
    : m_key(key)
    , m_buffer(0)
    , m_size(size)
{
}

IpcBuffer::~IpcBuffer()
{
    if (m_buffer) {
        shmdt(m_buffer);
        m_buffer = 0;
    }
}

void* IpcBuffer::buffer() const
{
    return m_buffer;
}
