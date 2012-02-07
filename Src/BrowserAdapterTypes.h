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

#ifndef BROWSERADAPTORTYPES_H
#define BROWSERADAPTORTYPES_H

namespace BATypes {

/**
 * The mode when mouse down/move/up events are received.
 */
enum MouseMode {
        MouseModeSelect = 0, // Select contents of page.
        MouseModeScroll = 1 // Scroll node at mouse
};

struct TouchPoint {
        enum State {
                TouchReleased,
                TouchPressed,
                TouchMoved,
                TouchStationary,
                TouchCancelled
        };

        TouchPoint()
                : id(0)
        , x(0)
                , y(0)
                , state(TouchStationary)
                { }

        TouchPoint(unsigned _id, State st, int screen_x, int screen_y)
                : id(_id)
        , x(screen_x)
                , y(screen_y)
                , state(st)
                { }

    unsigned id;
        int x;
        int y;
        State state;
};


enum FieldType
{
        FieldType_Text = 0,
        FieldType_Password,
        FieldType_Search,
        FieldType_Range,
        FieldType_Email,
        FieldType_Number,
        FieldType_Phone,
        FieldType_URL,
        FieldType_Color
};

// actions that can be handled by the currently focused field
enum FieldAction
{
        FieldAction_None                = 0x0,
        FieldAction_Done                = 0x1,  // obsolete. Kept for simpler submissions
        FieldAction_Next                = 0x2,
        FieldAction_Previous    = 0x8,
        FieldAction_Search              = 0x10  // obsolete. Kept for simpler submissions
};

// Misc flags & attributes of the currently focused field
enum FieldFlags
{
        FieldFlags_None                 = 0x0,
        FieldFlags_Emoticons    = 0x1   // show graphic emoticons in place of textual emoticons
};


// information about the currently focused field
struct EditorState {

        FieldType       type;
        FieldAction     actions;
        FieldFlags      flags;
        char            enterKeyLabel[40];      // utf8 encoded

        EditorState(FieldType inType = FieldType_Text, FieldAction inActions = FieldAction_None, FieldFlags inFlags = FieldFlags_None) :
                type(inType), actions(inActions), flags(inFlags)
        {
                enterKeyLabel[0] = 0;
        }

        bool operator==(const EditorState& other) const {
                return type == other.type && actions == other.actions && flags == other.flags && strncmp(enterKeyLabel, other.enterKeyLabel, sizeof(enterKeyLabel)) == 0;
        }
};

}

#endif
