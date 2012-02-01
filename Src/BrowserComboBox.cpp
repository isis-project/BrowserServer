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

#include "BrowserComboBox.h"

#include <QFile>
#include <QTimer>

#include <cjson/json.h>

static inline json_object* createJSONItem(const QWebSelectData& listData, int index)
{
    json_object* result = json_object_new_object();
    if (result) {
        json_object_object_add(result, const_cast<char*>("text"), json_object_new_string(listData.itemText(index).toUtf8().data()) );
        json_object_object_add(result, const_cast<char*>("isEnabled"), json_object_new_boolean(listData.itemIsEnabled(index)) );
        json_object_object_add(result, const_cast<char*>("isSeparator"), json_object_new_boolean(listData.itemType(index) == QWebSelectData::Separator) );
        json_object_object_add(result, const_cast<char*>("isLabel"), json_object_new_boolean(listData.itemType(index) == QWebSelectData::Group) );
    }
    return result;
}

static inline json_object* createJSONItemList(const QWebSelectData& listData, int& selectedIndex)
{
    json_object* result = json_object_new_array();
    if (result) {
        for (int i = 0; i < listData.itemCount(); ++i) {
            if (listData.itemIsSelected(i))
                selectedIndex = i;
            json_object_array_add(result, createJSONItem(listData, i));
        }
    }
    return result;
}


static inline json_object* createJSONPopupData(const QWebSelectData& listData)
{
    json_object* result = json_object_new_object();
    if (result) {
        int selectedIndex = -1;
        json_object_object_add(result, const_cast<char*>("items"), createJSONItemList(listData, selectedIndex));
        json_object_object_add(result, const_cast<char*>("selectedIdx"), json_object_new_int(selectedIndex) );

    }
    return result;
}

static inline QString writeJSONPopupData(int id, const QWebSelectData& listData)
{
    QFile file(QString("/tmp/ComboBoxPopup%1").arg(id));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QString();

    json_object* content = createJSONPopupData(listData);
    const char* data = json_object_get_string(content);
    file.write(data, strlen(data));
    json_object_put(content);

    return file.flush() ? file.fileName() : QString();
}

void BrowserComboBox::show(const QWebSelectData& data)
{
    QString fileName = writeJSONPopupData(m_id, data);
    if (fileName.isEmpty() || !m_server.showComboBoxPopup(m_id, fileName.toUtf8().constData()))
        QTimer::singleShot(0, this, SIGNAL(didHide()));
}

void BrowserComboBox::select(int index)
{
    if (index >= 0)
        emit selectItem(index, false, false);
    emit didHide();
}

void BrowserComboBoxList::selectItem(int id, int index)
{
    QMap<int, BrowserComboBox*>::iterator item = m_list.find(id);
    if (item != m_list.end())
        item.value()->select(index);
}

QWebSelectMethod* BrowserComboBoxList::createComboBox()
{
    static int idSeq = 0;
    BrowserComboBox* result = new BrowserComboBox(m_server, ++idSeq);
    connect(result, SIGNAL(destroyed()), this, SLOT(comboBoxDestroyed()));
    m_list.insert(result->id(), result);
    return result;
}

void BrowserComboBoxList::comboBoxDestroyed()
{
    BrowserComboBox* combo = qobject_cast<BrowserComboBox*>(sender());
    if (combo)
        m_list.remove(combo->id());
}
