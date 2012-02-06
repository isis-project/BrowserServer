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

#include <algorithm>
#include <string.h>

#include "BrowserRect.h"
#include <math.h>

BrowserRect doubleToIntRoundDown(const BrowserDoubleRect & rect)
{
    int x = ceil(rect.x());
    int y = ceil(rect.y());
    int r = floor(rect.r());
    int b = floor(rect.b());
    return BrowserRect(x, y, r-x, b-y);
}

BrowserRect doubleToIntRoundUp(const BrowserDoubleRect & rect)
{
    int x = floor(rect.x());
    int y = floor(rect.y());
    int r = ceil(rect.r());
    int b = ceil(rect.b());
    return BrowserRect(x, y, r-x, b-y);
}

