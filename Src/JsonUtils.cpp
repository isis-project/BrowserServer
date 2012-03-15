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

#include "JsonUtils.h"

bool jValueToJsonString(std::string& result, const pbnjson::JValue& val, const char* schema)
{
    pbnjson::JGenerator serializer;

    if (!serializer.toString(val, pbnjson::JSchemaFragment(schema), result)) {
        result.clear();
        return false;
    }
    return true;
}

bool jValueToJsonStringUsingSchemaFile(std::string& result, const pbnjson::JValue& val, const char* schemaFileName)
{
    pbnjson::JGenerator serializer;

    if (!serializer.toString(val, pbnjson::JSchemaFile(schemaFileName), result)) {
        result.clear();
        return false;
    }
    return true;
}

bool jsonStringToJValue(pbnjson::JValue& val, const std::string& json, const char* schema)
{
    pbnjson::JDomParser parser(NULL);

    if (!parser.parse(json, pbnjson::JSchemaFragment(schema), NULL))
        return false;

    val = parser.getDom();
    return true;
}

bool jsonStringToJValueUsingSchemaFile(pbnjson::JValue& val, const std::string& json, const char* schemaFileName)
{
    pbnjson::JDomParser parser(NULL);

    if (!parser.parse(json, pbnjson::JSchemaFile(schemaFileName), NULL))
        return false;

    val = parser.getDom();
    return true;
}

bool lsMessageToJValue(pbnjson::JValue& val, LSMessage* message, const char* schema)
{
    if (!message)
        return false;

    const char* payload = LSMessageGetPayload(message);
    return jsonStringToJValue(val, payload, schema);
}

