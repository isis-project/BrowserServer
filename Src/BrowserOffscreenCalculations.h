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

#ifndef BROWSEROFFSCREENCALCULATIONS_H
#define BROWSEROFFSCREENCALCULATIONS_H

struct BrowserOffscreenCalculations
{
    BrowserOffscreenCalculations() {
        reset();
    }

    void reset() {
        bufferWidth = 0;
        bufferHeight = 0;

        contentZoom = 1.0;

        viewportWidth = 0;
        viewportHeight = 0;
        contentWidth = 0;
        contentHeight = 0;

        renderX = 0;
        renderY = 0;
        renderWidth = 0;
        renderHeight = 0;
    }

    int bufferWidth;
    int bufferHeight;

    double contentZoom;

    int viewportWidth;
    int viewportHeight;
    int contentWidth;
    int contentHeight;

    int renderX;
    int renderY;
    int renderWidth;
    int renderHeight;
};

#endif /* BROWSEROFFSCREENCALCULATIONS_H */
