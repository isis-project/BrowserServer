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

#ifndef BROWSEROFFSCREENINFO_H
#define BROWSEROFFSCREENINFO_H

struct BrowserOffscreenInfo
{
    // The buffer dimensions. the full height may not have been rendered.
    // Use contentHeight to figure out the height rendered
    int bufferWidth;
    int bufferHeight;

    // At what zoom factor was the page rendered into the buffer
    double contentZoom;

    // The portion of the webpage that has been rendered into the buffer. The zoom factor
    // is baked into these dimensions. For eg: renderedX = renderedZoom * pageX
    // renderedWidth == bufferWidth;
    // renderedHeight == renderedHeight (<= bufferHeight)
    int renderedX;
    int renderedY;
    int renderedWidth;
    int renderedHeight;
};

#endif
