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

#include "Settings.h"
#include <QtCore/QCoreApplication>
#include <weboswebsettings.h>


bool InitSettings()
{
    QVariantMap map;

    map.insert("CacheEnabled", true);
    map.insert("CacheMaxSize", "50M");
    map.insert("CachePath", "/media/cryptofs/.browser/cache");
    map.insert("CookieJarPath", "/media/cryptofs/.browser/cookies");
    map.insert("DownloadPath", "/media/internal/downloads");
    map.insert("MaxPixelsPerDecodedImage", 16777216);
    map.insert("RemoteInspectorPort", 0);
    map.insert("ImageResourcesPath","/usr/palm/webkit/images");
    map.insert("SelectionColor","#ffffcc");
    map.insert("HighlightedTextColor","#000000");

    // WebSettings
    map.insert("WebSettings/AcceleratedCompositingEnabled", true);
    map.insert("WebSettings/AutoLoadImages", true);
    map.insert("WebSettings/DnsPrefetchEnabled", true);
    map.insert("WebSettings/JavascriptEnabled", true);
    map.insert("WebSettings/OfflineStorageDefaultQuota", "5M");
    map.insert("WebSettings/OfflineWebApplicationCacheQuota", "10M");
    map.insert("WebSettings/PersistentStorageEnabled", true);
    map.insert("WebSettings/PersistentStoragePath", "/media/cryptofs/.browser");
    map.insert("WebSettings/PluginsEnabled", true);
    map.insert("WebSettings/PluginSupplementalPath","/usr/lib/BrowserServerPlugins");
    map.insert("WebSettings/PluginSupplementalUserPath","/media/cryptofs/apps/usr/lib/BrowserServerPlugins");
    map.insert("WebSettings/TiledBackingStoreEnabled", false);
    map.insert("WebSettings/WebGLEnabled", true);

    return webOS::WebSettings::initSettings(map);
}


bool InitWebSettings()
{
    return webOS::WebSettings::initWebSettings();
}

quint64 StringToBytes(const QString &srcString)
{
    return webOS::WebSettings::stringToBytes(srcString);
}
