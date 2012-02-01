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

#ifndef YAPPACKET_H
#define YAPPACKET_H

#include <stdint.h>

class YapPacket
{
public:

    int length() const;

    // write functions
    void operator<<(bool val);
    void operator<<(int8_t val);
    void operator<<(int16_t val);
    void operator<<(uint16_t val);
    void operator<<(int32_t val);
    void operator<<(int64_t val);
    void operator<<(double val);
    void operator<<(const char* val);

    // read functions
    void operator>>(bool& val);
    void operator>>(int8_t& val);
    void operator>>(int16_t& val);
    void operator>>(uint16_t& val);
    void operator>>(int32_t& val);
    void operator>>(int64_t& val);
    void operator>>(double& val);
    void operator>>(char*& val);

private:

    // Write only packet
    YapPacket(uint8_t* buffer);
    // Read only packet
    YapPacket(uint8_t* buffer, int readTotalLen);

    ~YapPacket() {}

    void setReadTotalLength(int len);
    void reset();

    YapPacket(const YapPacket&);
    YapPacket& operator=(const YapPacket&);
    
    uint8_t* m_buffer;
    bool  m_forWriting;
    int   m_currReadPos;
    int   m_readTotalLen;
    int   m_currWritePos;

    friend class YapProxy;
    friend class YapClient;
};

#endif /* YAPPACKET_H */
