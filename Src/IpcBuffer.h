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

#ifndef IPCBUFFER_H
#define IPCBUFFER_H

class IpcBuffer
{
public:

	static IpcBuffer* create(int size);
	static IpcBuffer* attach(int key, int size);
	~IpcBuffer();
	
	void* buffer() const;
    int size() const { return m_size; }
	int key() const { return m_key; }

protected:

	IpcBuffer(int key, int size);

	int m_key;
	void* m_buffer;
	int m_size;
};


#endif /* IPCBUFFER_H */
