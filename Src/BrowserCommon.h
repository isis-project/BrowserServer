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

#ifndef BROWSERCOMMON_H
#define BROWSERCOMMON_H

#include <stdio.h>


#if defined(DEBUG)
// Too verbose for now
#undef LOGGING_ENABLED
#elif defined(NDEBUG)
#undef LOGGING_ENABLED
#else
#error "Need to define DEBUG or NDEBUG"
#endif



//#define LOGGING_ENABLED
#if defined(LOGGING_ENABLED)
#define BDBG(...) \
do { \
    fprintf(stderr, "BDBG: [%s: %d]: ", __PRETTY_FUNCTION__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); \
} while (0)

#define BERR(...) \
do { \
    fprintf(stderr, "BERR: [%s: %d]: ", __PRETTY_FUNCTION__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); \
} while (0)

#else

#define BDBG(...)  (void)0
#define BERR(...)  (void)0

#endif



#endif /* BROWSERCOMMON_H */
