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

#ifndef YAPDEFS_H
#define YAPDEFS_H

#include <stdint.h>

#define kMaxMsgLen              ((int)16384) // increased to 16k up from 4k

#define kPacketFlagSyncMask     ((uint8_t)(1 << 0))
#define kPacketFlagCommandMask  ((uint8_t)(1 << 1))
#define kPacketFlagReplyMask    ((uint8_t)(1 << 2))
#define kPacketFlagMessageMask  ((uint8_t)(1 << 3))

#endif /* YAPDEFS_H */
