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

#include <pbnjson.hpp>

static void jsonToString(const pbnjson::JValue& val, std::string& result, const char* schema = "{}")
{
  pbnjson::JGenerator serializer;

  if (!serializer.toString(val, pbnjson::JSchemaFragment(schema), result)) {
    qCritical("jsonToString: failed to generate json result");
    result = "{}";
  }
}

static inline pbnjson::JValue createJSONItem(const QWebSelectData& listData, int index)
{
    pbnjson::JValue result = pbnjson::Object();

    result.put(const_cast<char*>("text"), listData.itemText(index).toUtf8().constData());
    result.put(const_cast<char*>("isEnabled"), listData.itemIsEnabled(index));
    result.put(const_cast<char*>("isSeparator"), listData.itemType(index) == QWebSelectData::Separator);
    result.put(const_cast<char*>("isLabel"), listData.itemType(index) == QWebSelectData::Group);

    return result;
}

static inline pbnjson::JValue createJSONItemList(const QWebSelectData& listData, int& selectedIndex)
{
    pbnjson::JValue result = pbnjson::Array();

    for (int i = 0; i < listData.itemCount(); ++i) {
      if (listData.itemIsSelected(i))
        selectedIndex = i;
      result.append(createJSONItem(listData, i));
    }

    return result;
}


static inline pbnjson::JValue createJSONPopupData(const QWebSelectData& listData)
{
    pbnjson::JValue result = pbnjson::Object();
    int selectedIndex = -1;

    result.put(const_cast<char*>("items"), createJSONItemList(listData, selectedIndex));
    result.put(const_cast<char*>("selectedIdx"), (int64_t) selectedIndex);

    return result;
}

static inline QString writeJSONPopupData(int id, const QWebSelectData& listData)
{
    QFile file(QString("/tmp/ComboBoxPopup%1").arg(id));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QString();

    pbnjson::JValue content = createJSONPopupData(listData);
    std::string result;
    jsonToString(content, result);
    const char* data = result.c_str();
    file.write(data, strlen(data));

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
