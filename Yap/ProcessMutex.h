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

#ifndef PROCESSMUTEX_H
#define PROCESSMUTEX_H

class ProcessMutex
{
public:

    ProcessMutex(int dataSize, int key = -1);
    ~ProcessMutex();

    int key() const { return m_key; }

    bool isValid() const;

    bool tryLock(int numTries=10);
    void lock();
    void unlock();

    void* data() const;

private:

    int m_key;
    void* m_data;
    int m_dataSize;
};

#endif /* PROCESSMUTEX_H */
