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

#ifndef BrowserComboBox_h
#define BrowserComboBox_h

#include "qwebkitplatformplugin.h"
#include "WebOSPlatformPlugin.h"

#include <QMap>

class BrowserComboBoxServer {
public:
    virtual bool showComboBoxPopup(int id, const char* fileName) = 0;
    virtual void hideComboBoxPopup(int id) = 0;
};

class BrowserComboBox : public QWebSelectMethod {
    Q_OBJECT
public:
    BrowserComboBox(BrowserComboBoxServer& server, int id) : m_server(server), m_id(id) {};
    virtual void show(const QWebSelectData&);
    virtual void hide() { m_server.hideComboBoxPopup(m_id); }
    virtual void setGeometry(const QRect&) {}
    virtual void setFont(const QFont&) {}

    int id() const { return m_id; }
    void select(int index);

private:
    BrowserComboBoxServer& m_server;
    const int m_id;
};

class BrowserComboBoxList : public QObject, public WebOSPlatformPlugin::ComboBoxFactory {
    Q_OBJECT
public:
    BrowserComboBoxList(BrowserComboBoxServer& server) : m_server(server) {}
    void selectItem(int id, int index);

    virtual QWebSelectMethod* createComboBox();

private Q_SLOTS:
    void comboBoxDestroyed();

private:
    BrowserComboBoxServer& m_server;
    QMap<int, BrowserComboBox*> m_list;
};

#endif // BrowserComboBox_h
