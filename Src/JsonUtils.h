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
 * LICENSE@@@ */

#ifndef JSONUTILS_H
#define JSONUTILS_H

#include <string>
#include <pbnjson.hpp>

bool jValueToJsonString(std::string&, const pbnjson::JValue&, const char* schema = "{}");
bool jValueToJsonStringUsingSchemaFile(std::string&, const pbnjson::JValue&, const char*);
bool jsonStringToJValue(pbnjson::JValue&, const std::string&, const char* schema = "{}");
bool jsonStringToJValueUsingSchemaFile(pbnjson::JValue&, const std::string&, const char*);

#ifdef USE_LUNA_SERVICE
#include <lunaservice.h>
bool lsMessageToJValue(pbnjson::JValue&, LSMessage*, const char* schema = "{}");
#endif /* USE_LUNA_SERVICE */

#endif /* JSONUTILS_H */
