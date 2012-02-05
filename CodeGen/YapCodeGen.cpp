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

#include <QString>
#include <QStringList>
#include <QPair>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

enum YapType {
    YapBool,
    YapByte,
    YapShort,
    YapUShort,
    YapInt,
    YapLong,
    YapDouble,
    YapString
};

typedef QPair<YapType, QString> TypeArgPair;
typedef QList<TypeArgPair>      TypeArgPairList;

struct YapSyncCmd {
    QString cmd;
    QString cmdValue;
    TypeArgPairList inArgs;
    TypeArgPairList outArgs;
};

struct YapAsyncCmd {
    QString cmd;
    QString cmdValue;
    TypeArgPairList inArgs;
};

struct YapMsg {
    QString msg;
    QString msgValue;
    TypeArgPairList inArgs;
};

static QList<YapSyncCmd>  gSyncCmdList;
static QList<YapAsyncCmd> gAsyncCmdList;
static QList<YapMsg>      gMsgList;

static bool
ignoreLine(const char* line)
{
    if (!line)
        return true;

    if (line[0] == '#')
        return true;

    const char* ptr = line;
    while (*ptr != 0 && *ptr != '\n') {
        if (*ptr != ' ')
            return false;
    }

    return true;
}

static bool
parseCmdOrMsgAndValuePair(QString str, QString& cmdOrMsg, QString& value)
{
    str = str.trimmed();
    if (str.isEmpty())
        return false;

    QStringList splitList = str.split(",", QString::SkipEmptyParts);
    if (splitList.size() != 2)
        return false;

    cmdOrMsg = splitList.at(0).trimmed();
    value    = splitList.at(1).trimmed();

    return true;
}

static bool
parseTypeArgPairList(QString str, TypeArgPairList& retList)
{
    str = str.trimmed();
    if (str.isEmpty())
        return true;

    QStringList argsList = str.split(",", QString::SkipEmptyParts);
    for (int i = 0; i < argsList.size(); i++) {
        QString s = argsList.at(i).trimmed();
        if (s.isEmpty())
            continue;

        QStringList typeAndArg = s.split(" ", QString::KeepEmptyParts);
        if (typeAndArg.size() != 2)
            return false;

        QString type = typeAndArg.at(0).trimmed();
        QString arg  = typeAndArg.at(1).trimmed();

        if (type == "bool")
            retList.append(TypeArgPair(YapBool, arg));
        else if (type == "byte")
            retList.append(TypeArgPair(YapByte, arg));
        else if (type == "short")
            retList.append(TypeArgPair(YapShort, arg));
        else if (type == "uint16_t")
            retList.append(TypeArgPair(YapUShort, arg));
        else if (type == "int")
            retList.append(TypeArgPair(YapInt, arg));
        else if (type == "long")
            retList.append(TypeArgPair(YapLong, arg));
        else if (type == "double")
            retList.append(TypeArgPair(YapDouble, arg));
        else if (type == "string")
            retList.append(TypeArgPair(YapString, arg));
        else {
            fprintf(stderr, "Unknown type %s specified\n", type.toLocal8Bit().constData());
            return false;
        }
    }

    return true;
}

static void
printInputTypeArgPair(FILE* f, TypeArgPair pair)
{
    switch (pair.first) {
    case (YapBool): fprintf(f, "bool %s", pair.second.toLocal8Bit().constData()); break;
    case (YapByte): fprintf(f, "int8_t %s", pair.second.toLocal8Bit().constData()); break;
    case (YapShort): fprintf(f, "int16_t %s", pair.second.toLocal8Bit().constData()); break;
    case (YapUShort): fprintf(f, "uint16_t %s", pair.second.toLocal8Bit().constData()); break;
    case (YapInt): fprintf(f, "int32_t %s", pair.second.toLocal8Bit().constData()); break;
    case (YapLong): fprintf(f, "int64_t %s", pair.second.toLocal8Bit().constData()); break;
    case (YapDouble): fprintf(f, "double %s", pair.second.toLocal8Bit().constData()); break;
    case (YapString): fprintf(f, "const char* %s", pair.second.toLocal8Bit().constData()); break;
    }
}

static void
printOutputTypeArgPair(FILE* f, TypeArgPair pair)
{
    switch (pair.first) {
    case (YapBool): fprintf(f, "bool& %s", pair.second.toLocal8Bit().constData()); break;
    case (YapByte): fprintf(f, "int8_t& %s", pair.second.toLocal8Bit().constData()); break;
    case (YapShort): fprintf(f, "int16_t& %s", pair.second.toLocal8Bit().constData()); break;
    case (YapUShort): fprintf(f, "uint16_t& %s", pair.second.toLocal8Bit().constData()); break;
    case (YapInt): fprintf(f, "int32_t& %s", pair.second.toLocal8Bit().constData()); break;
    case (YapLong): fprintf(f, "int64_t& %s", pair.second.toLocal8Bit().constData()); break;
    case (YapDouble): fprintf(f, "double& %s", pair.second.toLocal8Bit().constData()); break;
    case (YapString): fprintf(f, "char*& %s", pair.second.toLocal8Bit().constData()); break;
    }
}

static void
printTypeArgPair(FILE* f, TypeArgPair pair)
{
    switch (pair.first) {
    case (YapBool): fprintf(f, "bool %s = 0", pair.second.toLocal8Bit().constData()); break;
    case (YapByte): fprintf(f, "int8_t %s = 0", pair.second.toLocal8Bit().constData()); break;
    case (YapShort): fprintf(f, "int16_t %s = 0", pair.second.toLocal8Bit().constData()); break;
    case (YapUShort): fprintf(f, "uint16_t %s = 0", pair.second.toLocal8Bit().constData()); break;
    case (YapInt): fprintf(f, "int32_t %s = 0", pair.second.toLocal8Bit().constData()); break;
    case (YapLong): fprintf(f, "int64_t %s = 0", pair.second.toLocal8Bit().constData()); break;
    case (YapDouble): fprintf(f, "double %s = 0", pair.second.toLocal8Bit().constData()); break;
    case (YapString): fprintf(f, "char* %s = 0", pair.second.toLocal8Bit().constData()); break;
    }
}

static void
printInputTypeArgPairConverted(FILE* f, TypeArgPair pair, const char* source)
{
    switch (pair.first) {
    case (YapBool):
        fprintf(f, "bool %s = (strcasecmp(%s, \"true\") == 0);",
                pair.second.toLocal8Bit().constData(), source);
        break;
    case (YapByte):
        fprintf(f, "int8_t %s = atoi(%s);",
                pair.second.toLocal8Bit().constData(), source);
        break;
    case (YapShort):
        fprintf(f, "int16_t %s = atoi(%s);",
                pair.second.toLocal8Bit().constData(), source);
        break;
    case (YapUShort):
        fprintf(f, "uint16_t %s = strtoul(%s, NULL, 0);",
                pair.second.toLocal8Bit().constData(), source);
        break;
    case (YapInt):
        fprintf(f, "int32_t %s = atol(%s);",
                pair.second.toLocal8Bit().constData(), source);
        break;
    case (YapLong):
        fprintf(f, "int64_t %s = atoll(%s);",
                pair.second.toLocal8Bit().constData(), source);
        break;
    case (YapDouble):
        fprintf(f, "double %s = strtod(%s, NULL);",
                pair.second.toLocal8Bit().constData(), source);
        break;
    case (YapString):
        fprintf(f, "char* %s = %s;",
                pair.second.toLocal8Bit().constData(), source); break;
        break;
    }
}

static
void writeServerHeaderFile(QString className)
{
    FILE* f = fopen((className + ".h").toLocal8Bit().constData(), "w");
    if (!f) {
        fprintf(stderr, "Failed to open %s for writing\n", className.toLocal8Bit().constData());
        exit(-1);
    }

    fprintf(f, "// This file is autogenerated from YapCodeGen. Do not Edit \n\n");
    fprintf(f, "#ifndef %s_H\n", className.toUpper().toLocal8Bit().constData());
    fprintf(f, "#define %s_H\n", className.toUpper().toLocal8Bit().constData());
    fprintf(f, "\n");
    fprintf(f, "#include <YapServer.h>\n");
    fprintf(f, "#include <YapProxy.h>\n");
    fprintf(f, "#include <YapPacket.h>\n");
    fprintf(f, "\n");

    fprintf(f, "class %s : public YapServer\n", className.toLocal8Bit().constData());
    fprintf(f, "{\n");

    fprintf(f, "public:\n\n");
    fprintf(f, "    %s(const char* name) : YapServer(name) {}\n", className.toLocal8Bit().constData());
    fprintf(f, "    virtual ~%s() {}\n\n", className.toLocal8Bit().constData());

    fprintf(f, "    // Async Messages\n");
    for (int i = 0; i < gMsgList.size(); i++) {
        YapMsg y = gMsgList.at(i);
        fprintf(f, "    void msg%s(YapProxy* proxy", y.msg.toLocal8Bit().constData());
        for (int j = 0; j < y.inArgs.size(); j++) {
            fprintf(f, ", ");
            printInputTypeArgPair(f, y.inArgs.at(j));
        }
        fprintf(f, ");\n");
    }

    fprintf(f, "\n");
    fprintf(f, "protected:\n\n");
    fprintf(f, "    virtual void handleSyncCommand(YapProxy* proxy, YapPacket* cmd, YapPacket* reply);\n");
    fprintf(f, "    virtual void handleAsyncCommand(YapProxy* proxy, YapPacket* cmd);\n");

    fprintf(f, "\n");
    fprintf(f, "    // Sync Commands\n");
    for (int i = 0; i < gSyncCmdList.size(); i++) {
        YapSyncCmd y = gSyncCmdList.at(i);
        fprintf(f, "    virtual void syncCmd%s(YapProxy* proxy", y.cmd.toLocal8Bit().constData());
        for (int j = 0; j < y.inArgs.size(); j++) {
            fprintf(f, ", ");
            printInputTypeArgPair(f, y.inArgs.at(j));
        }
        for (int j = 0; j < y.outArgs.size(); j++) {
            fprintf(f, ", ");
            printOutputTypeArgPair(f, y.outArgs.at(j));
        }
        fprintf(f, ") = 0;\n");
    }

    fprintf(f, "\n");
    fprintf(f, "    // Async Commands\n");
    for (int i = 0; i < gAsyncCmdList.size(); i++) {
        YapAsyncCmd y = gAsyncCmdList.at(i);
        fprintf(f, "    virtual void asyncCmd%s(YapProxy* proxy", y.cmd.toLocal8Bit().constData());
        for (int j = 0; j < y.inArgs.size(); j++) {
            fprintf(f, ", ");
            printInputTypeArgPair(f, y.inArgs.at(j));
        }
        fprintf(f, ") = 0;\n");
    }

    fprintf(f, "};\n\n");
    fprintf(f, "#endif // %s_H \n", className.toUpper().toLocal8Bit().constData());

    fclose(f);
}

static
void writeServerCppFile(QString className)
{
    FILE* f = fopen((className + ".cpp").toLocal8Bit().constData(), "w");
    if (!f) {
        fprintf(stderr, "Failed to open %s for writing\n", className.toLocal8Bit().constData());
        exit(-1);
    }

    // File header
    fprintf(f, "// This file is autogenerated from YapCodeGen. Do not Edit \n\n");
    fprintf(f, "#include <stdlib.h>\n");
    fprintf(f, "#include <stdio.h>\n");
    fprintf(f, "#include <%s.h>\n", className.toLocal8Bit().constData());
    fprintf(f, "\n");

    // Sync Command
    fprintf(f, "void %s::handleSyncCommand(YapProxy* proxy, YapPacket* cmd, YapPacket* reply)\n",
           className.toLocal8Bit().constData());
    fprintf(f, "{\n");
    fprintf(f, "\tint16_t cmdValue;\n");
    fprintf(f, "\n");
    fprintf(f, "\t(*cmd) >> cmdValue;\n");
    fprintf(f, "\n");
    fprintf(f, "\tswitch (cmdValue) {\n");
    for (int i = 0; i < gSyncCmdList.size(); i++) {
        YapSyncCmd y = gSyncCmdList.at(i);
        fprintf(f, "\tcase %s: { // %s\n",
               y.cmdValue.toLocal8Bit().constData(),
               y.cmd.toLocal8Bit().constData());

        QStringList stringsToFree;

        fprintf(f, "\t\t\n");
        for (int j = 0; j < y.inArgs.size(); j++) {
            fprintf(f, "\t\t");
            printTypeArgPair(f, y.inArgs.at(j));
            fprintf(f, ";\n");
            if (y.inArgs.at(j).first == YapString)
                stringsToFree.append(y.inArgs.at(j).second);
        }


        fprintf(f, "\t\t\n");
        for (int j = 0; j < y.outArgs.size(); j++) {
            fprintf(f, "\t\t");
            printTypeArgPair(f, y.outArgs.at(j));
            fprintf(f, ";\n");
            if (y.outArgs.at(j).first == YapString)
                stringsToFree.append(y.outArgs.at(j).second);
        }

        fprintf(f, "\t\t\n");
        for (int j = 0; j < y.inArgs.size(); j++) {
            fprintf(f, "\t\t(*cmd) >> %s;\n",
                   y.inArgs.at(j).second.toLocal8Bit().constData());
        }

        fprintf(f, "\t\t\n");
        fprintf(f, "\t\tsyncCmd%s(proxy", y.cmd.toLocal8Bit().constData());
        for (int j = 0; j < y.inArgs.size(); j++) {
            fprintf(f, ", %s", y.inArgs.at(j).second.toLocal8Bit().constData());
        }
        for (int j = 0; j < y.outArgs.size(); j++) {
            fprintf(f, ", %s", y.outArgs.at(j).second.toLocal8Bit().constData());
        }
        fprintf(f, ");\n");

        fprintf(f, "\t\t\n");
        for (int j = 0; j < y.outArgs.size(); j++) {
            fprintf(f, "\t\t(*reply) << %s;\n",
                   y.outArgs.at(j).second.toLocal8Bit().constData());
        }

        fprintf(f, "\t\t\n");
        for (int j = 0; j < stringsToFree.size(); j++) {
            fprintf(f, "\t\tif (%s) free(%s);\n",
                   stringsToFree.at(j).toLocal8Bit().constData(),
                   stringsToFree.at(j).toLocal8Bit().constData());
        }

        fprintf(f, "\t\t\n");
        fprintf(f, "\t\tbreak;\n");
        fprintf(f, "\t}\n");
    }
    fprintf(f, "\tdefault:\n");
    fprintf(f, "\t\tfprintf(stderr, \"Unknown sync cmd: %%d\\n\", cmdValue);\n");
    fprintf(f, "\t}\n");
    fprintf(f, "}\n\n");

    // Async Commands
    fprintf(f, "void %s::handleAsyncCommand(YapProxy* proxy, YapPacket* cmd)\n",
           className.toLocal8Bit().constData());
    fprintf(f, "{\n");
    fprintf(f, "\tint16_t cmdValue;\n");
    fprintf(f, "\n");
    fprintf(f, "\t(*cmd) >> cmdValue;\n");
    fprintf(f, "\n");
    fprintf(f, "\tswitch (cmdValue) {\n");
    for (int i = 0; i < gAsyncCmdList.size(); i++) {
        YapAsyncCmd y = gAsyncCmdList.at(i);
        fprintf(f, "\tcase %s: { // %s\n",
               y.cmdValue.toLocal8Bit().constData(),
               y.cmd.toLocal8Bit().constData());

        QStringList stringsToFree;

        fprintf(f, "\t\t\n");
        for (int j = 0; j < y.inArgs.size(); j++) {
            fprintf(f, "\t\t");
            printTypeArgPair(f, y.inArgs.at(j));
            fprintf(f, ";\n");
            if (y.inArgs.at(j).first == YapString)
                stringsToFree.append(y.inArgs.at(j).second);
        }

        fprintf(f, "\t\t\n");
        for (int j = 0; j < y.inArgs.size(); j++) {
            fprintf(f, "\t\t(*cmd) >> %s;\n",
                   y.inArgs.at(j).second.toLocal8Bit().constData());
        }

        fprintf(f, "\t\t\n");
        fprintf(f, "\t\tasyncCmd%s(proxy", y.cmd.toLocal8Bit().constData());
        for (int j = 0; j < y.inArgs.size(); j++) {
            fprintf(f, ", %s", y.inArgs.at(j).second.toLocal8Bit().constData());
        }
        fprintf(f, ");\n");

        fprintf(f, "\t\t\n");
        for (int j = 0; j < stringsToFree.size(); j++) {
            fprintf(f, "\t\tif (%s) free(%s);\n",
                   stringsToFree.at(j).toLocal8Bit().constData(),
                   stringsToFree.at(j).toLocal8Bit().constData());
        }

        fprintf(f, "\t\t\n");
        fprintf(f, "\t\tbreak;\n");
        fprintf(f, "\t}\n");
    }
    fprintf(f, "\tdefault:\n");
    fprintf(f, "\t\tfprintf(stderr, \"Unknown async cmd: %%d\\n\", cmdValue);\n");
    fprintf(f, "\t}\n");
    fprintf(f, "}\n\n");

    // Async messages
    for (int i = 0; i < gMsgList.size(); i++) {
        YapMsg y = gMsgList.at(i);
        fprintf(f, "void %s::msg%s(YapProxy* proxy",
               className.toLocal8Bit().constData(),
               y.msg.toLocal8Bit().constData());
        for (int j = 0; j < y.inArgs.size(); j++) {
            fprintf(f, ", ");
            printInputTypeArgPair(f, y.inArgs.at(j));
        }
        fprintf(f, ")\n");
        fprintf(f, "{\n");
        fprintf(f, "\tYapPacket* pkt = proxy->packetMessage();\n");
        fprintf(f, "\t(*pkt) << (int16_t) %s; // %s\n",
               y.msgValue.toLocal8Bit().constData(),
               y.msg.toLocal8Bit().constData());
        for (int j = 0; j < y.inArgs.size(); j++) {
            fprintf(f, "\t(*pkt) << %s;\n",
                   y.inArgs.at(j).second.toLocal8Bit().constData());
        }
        fprintf(f, "\tproxy->sendMessage();\n");
        fprintf(f, "}\n\n");
    }

    fclose(f);
}


static
void writeClientHeaderFile(QString className)
{
    FILE* f = fopen((className + ".h").toLocal8Bit().constData(), "w");
    if (!f) {
        fprintf(stderr, "Failed to open %s for writing\n", className.toLocal8Bit().constData());
        exit(-1);
    }

    fprintf(f, "// This file is autogenerated from YapCodeGen. Do not Edit \n\n");
    fprintf(f, "#ifndef %s_H\n", className.toUpper().toLocal8Bit().constData());
    fprintf(f, "#define %s_H\n", className.toUpper().toLocal8Bit().constData());
    fprintf(f, "\n");

    fprintf(f, "#include <YapClient.h>\n");
    fprintf(f, "#include <YapPacket.h>\n");
    fprintf(f, "\n");

    fprintf(f, "class %s : public YapClient\n", className.toLocal8Bit().constData());
    fprintf(f, "{\n");

    fprintf(f, "public:\n\n");
    fprintf(f, "\t%s(const char* name) : YapClient(name) {}\n", className.toLocal8Bit().constData());
    fprintf(f, "\t%s(const char* name, GMainContext *ctxt) : YapClient(name, ctxt) {}\n", className.toLocal8Bit().constData());
    fprintf(f, "\tvirtual ~%s() {}\n\n", className.toLocal8Bit().constData());

    fprintf(f, "\n");
    fprintf(f, "\t// Async commands\n");
    for (int i = 0; i < gAsyncCmdList.size(); i++) {
        YapAsyncCmd y = gAsyncCmdList.at(i);
        fprintf(f, "\tvoid asyncCmd%s(", y.cmd.toLocal8Bit().constData());
        for (int j = 0; j < y.inArgs.size(); j++) {
            if (j != 0)
                fprintf(f, ", ");
            printInputTypeArgPair(f, y.inArgs.at(j));
        }
        fprintf(f, ");\n");
    }

    fprintf(f, "\n");
    fprintf(f, "\t// Sync commands\n");
    for (int i = 0; i < gSyncCmdList.size(); i++) {
        YapSyncCmd y = gSyncCmdList.at(i);
        fprintf(f, "\tvoid syncCmd%s(", y.cmd.toLocal8Bit().constData());
        bool firstArg = true;
        for (int j = 0; j < y.inArgs.size(); j++) {
            if (j != 0)
                fprintf(f, ", ");
            printInputTypeArgPair(f, y.inArgs.at(j));
            firstArg = false;
        }
        for (int j = 0; j < y.outArgs.size(); j++) {
            if (!firstArg)
                fprintf(f, ", ");
            printOutputTypeArgPair(f, y.outArgs.at(j));
            firstArg = false;
        }
        fprintf(f, ");\n");
    }

    fprintf(f, "\n");
    fprintf(f, "\t// Raw command\n");
    fprintf(f, "\tbool sendRawCmd(const char* rawCmd);");

    fprintf(f, "\n");
    fprintf(f, "protected:\n\n");
    fprintf(f, "\t// Async Messages\n");
    for (int i = 0; i < gMsgList.size(); i++) {
        YapMsg y = gMsgList.at(i);
        fprintf(f, "\tvirtual void msg%s(", y.msg.toLocal8Bit().constData());
        for (int j = 0; j < y.inArgs.size(); j++) {
            if (j != 0)
                fprintf(f, ", ");
            printInputTypeArgPair(f, y.inArgs.at(j));
        }
        fprintf(f, ") = 0;\n");
    }

    fprintf(f, "\n");
    fprintf(f, "\t// Overriden functions\n");
    fprintf(f, "\tvirtual void handleAsyncMessage(YapPacket* msg);\n");

    fprintf(f, "};\n\n");
    fprintf(f, "#endif // %s_H \n", className.toUpper().toLocal8Bit().constData());

    fclose(f);
}

static
void writeClientCppFile(QString className)
{
    FILE* f = fopen((className + ".cpp").toLocal8Bit().constData(), "w");
    if (!f) {
        fprintf(stderr, "Failed to open %s for writing\n", className.toLocal8Bit().constData());
        exit(-1);
    }

    // File header
    fprintf(f, "// This file is autogenerated from YapCodeGen. Do not Edit \n\n");
    fprintf(f, "#include <stdlib.h>\n");
    fprintf(f, "#include <stdio.h>\n");
    fprintf(f, "#include <string.h>\n");
    fprintf(f, "#include <strings.h>\n");
    fprintf(f, "#include <glib.h>\n");
    fprintf(f, "#include <%s.h>\n", className.toLocal8Bit().constData());
    fprintf(f, "\n");


    // Sync commands
    for (int i = 0; i < gSyncCmdList.size(); i++) {
        YapSyncCmd y = gSyncCmdList.at(i);
        fprintf(f, "void %s::syncCmd%s(",
                className.toLocal8Bit().constData(),
                y.cmd.toLocal8Bit().constData());
        bool firstArg = true;
        for (int j = 0; j < y.inArgs.size(); j++) {
            if (j != 0)
                fprintf(f, ", ");
            printInputTypeArgPair(f, y.inArgs.at(j));
            firstArg = false;
        }
        for (int j = 0; j < y.outArgs.size(); j++) {
            if (!firstArg)
                fprintf(f, ", ");
            printOutputTypeArgPair(f, y.outArgs.at(j));
            firstArg = false;
        }
        fprintf(f, ")\n");
        fprintf(f, "{\n");
        fprintf(f, "\tYapPacket* cmd   = packetCommand();\n");
        fprintf(f, "\tYapPacket* reply = packetReply();\n");
        fprintf(f, "\t(*cmd) << (int16_t) %s; // %s\n",
                y.cmdValue.toLocal8Bit().constData(),
                y.cmd.toLocal8Bit().constData());
        for (int j = 0; j < y.inArgs.size(); j++) {
            fprintf(f, "\t(*cmd) << %s;\n",
                    y.inArgs.at(j).second.toLocal8Bit().constData());
        }
        fprintf(f, "\tsendSyncCommand();\n");
        for (int j = 0; j < y.outArgs.size(); j++) {
            fprintf(f, "\t(*reply) >> %s;\n",
                    y.outArgs.at(j).second.toLocal8Bit().constData());
        }
        fprintf(f, "}\n\n");
    }

    // ASync commands
    for (int i = 0; i < gAsyncCmdList.size(); i++) {
        YapAsyncCmd y = gAsyncCmdList.at(i);
        fprintf(f, "void %s::asyncCmd%s(",
                className.toLocal8Bit().constData(),
                y.cmd.toLocal8Bit().constData());
        for (int j = 0; j < y.inArgs.size(); j++) {
            if (j != 0)
                fprintf(f, ", ");
            printInputTypeArgPair(f, y.inArgs.at(j));
        }
        fprintf(f, ")\n");
        fprintf(f, "{\n");
        fprintf(f, "\tYapPacket* _cmd = packetCommand();\n");
        fprintf(f, "\t(*_cmd) << (int16_t) %s; // %s\n",
                y.cmdValue.toLocal8Bit().constData(),
                y.cmd.toLocal8Bit().constData());
        for (int j = 0; j < y.inArgs.size(); j++) {
            fprintf(f, "\t(*_cmd) << %s;\n",
                    y.inArgs.at(j).second.toLocal8Bit().constData());
        }
        fprintf(f, "\tsendAsyncCommand();\n");
        fprintf(f, "}\n\n");
    }

    // Raw Command
    fprintf(f, "bool %s::sendRawCmd(const char* rawCmd)\n",
            className.toLocal8Bit().constData());
    fprintf(f, "{\n");

    fprintf(f, "\tgchar** strSplit = g_strsplit(rawCmd, \" \", 0);\n\n");

    fprintf(f, "\tint argCount = 0;\n");
    fprintf(f, "\twhile (strSplit[argCount] != 0) argCount++;\n");
    fprintf(f, "\tif (argCount == 0) return false;\n\n");

    fprintf(f, "\tbool matched = false;\n\n");

    // Raw to Async command
    for (int i = 0; i < gAsyncCmdList.size(); i++) {
        YapAsyncCmd y = gAsyncCmdList.at(i);
        fprintf(f, "\tif (!matched && (strcmp(strSplit[0], \"%s\") == 0)) {\n",
                y.cmd.toLocal8Bit().constData());

        fprintf(f, "\t\tif ((argCount - 1) < %d) return false;\n",
                y.inArgs.size());
        fprintf(f, "\t\tmatched = true;\n\n");

        for (int j = 0; j < y.inArgs.size(); j++) {
            QString source = "strSplit[";
            source += QString::number(j + 1);
            source += "]";

            fprintf(f, "\t\t");
            printInputTypeArgPairConverted(f, y.inArgs.at(j),
                                           source.toLocal8Bit().constData());
            fprintf(f, "\n");
        }

        fprintf(f, "\n");
        fprintf(f, "\t\tasyncCmd%s(", y.cmd.toLocal8Bit().constData());
        for (int j = 0; j < y.inArgs.size(); j++) {
            if (j != 0)
                fprintf(f, ", ");
            fprintf(f, "%s", y.inArgs.at(j).second.toLocal8Bit().constData());
        }
        fprintf(f, ");\n");

        fprintf(f, "\t}\n\n");
    }

    // Raw to sync command
    for (int i = 0; i < gSyncCmdList.size(); i++) {
        YapSyncCmd y = gSyncCmdList.at(i);
        fprintf(f, "\tif (!matched && (strcmp(strSplit[0], \"%s\") == 0)) {\n",
                y.cmd.toLocal8Bit().constData());

        fprintf(f, "\t\tif ((argCount - 1) < %d) return false;\n",
                y.inArgs.size());
        fprintf(f, "\t\tmatched = true;\n");

        fprintf(f, "\n");
        for (int j = 0; j < y.inArgs.size(); j++) {
            QString source = "strSplit[";
            source += QString::number(j + 1);
            source += "]";

            fprintf(f, "\t\t");
            printInputTypeArgPairConverted(f, y.inArgs.at(j),
                                           source.toLocal8Bit().constData());
            fprintf(f, "\n");
        }

        fprintf(f, "\n");
        for (int j = 0; j < y.outArgs.size(); j++) {
            fprintf(f, "\t\t");
            printTypeArgPair(f, y.outArgs.at(j));
            fprintf(f, ";\n");
        }

        fprintf(f, "\n");
        fprintf(f, "\t\tsyncCmd%s(", y.cmd.toLocal8Bit().constData());
        bool firstArg = true;
        for (int j = 0; j < y.inArgs.size(); j++) {
            if (firstArg)
                firstArg = false;
            else
                fprintf(f, ", ");
            fprintf(f, "%s", y.inArgs.at(j).second.toLocal8Bit().constData());
        }
        for (int j = 0; j < y.outArgs.size(); j++) {
            if (firstArg)
                firstArg = false;
            else
                fprintf(f, ", ");
            fprintf(f, "%s", y.outArgs.at(j).second.toLocal8Bit().constData());
        }
        fprintf(f, ");\n");

        fprintf(f, "\t}\n\n");
    }

    fprintf(f, "\tg_strfreev(strSplit);\n");
    fprintf(f, "\treturn matched;\n");
    fprintf(f, "}\n\n");

    // Async messages
    fprintf(f, "void %s::handleAsyncMessage(YapPacket* _msg)\n",
            className.toLocal8Bit().constData());
    fprintf(f, "{\n");
    fprintf(f, "\tint16_t msgValue;\n");
    fprintf(f, "\n");
    fprintf(f, "\t(*_msg) >> msgValue;\n");
    fprintf(f, "\n");
    fprintf(f, "\tswitch (msgValue) {\n");
    for (int i = 0; i < gMsgList.size(); i++) {
        YapMsg y = gMsgList.at(i);
        fprintf(f, "\tcase %s: { // %s\n",
                y.msgValue.toLocal8Bit().constData(),
                y.msg.toLocal8Bit().constData());

        fprintf(f, "\n");
        for (int j = 0; j < y.inArgs.size(); j++) {
            fprintf(f, "\t\t");
            printTypeArgPair(f, y.inArgs.at(j));
            fprintf(f, ";\n");
        }

        fprintf(f, "\n");
        for (int j = 0; j < y.inArgs.size(); j++) {
            fprintf(f, "\t\t(*_msg) >> %s;\n",
                    y.inArgs.at(j).second.toLocal8Bit().constData());
        }

        fprintf(f, "\n");
        fprintf(f, "\t\tmsg%s(", y.msg.toLocal8Bit().constData());
        for (int j = 0; j < y.inArgs.size(); j++) {
            if (j != 0)
                fprintf(f, ", ");
            fprintf(f, "%s", y.inArgs.at(j).second.toLocal8Bit().constData());
        }
        fprintf(f, ");\n");

        // Free the strings
        for (int j = 0; j < y.inArgs.size(); j++) {
            if(y.inArgs.at(j).first == YapString) {
                fprintf(f, "\t\tfree(%s);\n", y.inArgs.at(j).second.toLocal8Bit().constData());
            }
        }

        fprintf(f, "\t\tbreak;\n");
        fprintf(f, "\t}\n");
    }
    fprintf(f, "\tdefault:\n");
    fprintf(f, "\t\tfprintf(stderr, \"Unknown msg: 0x%%04x\\n\", msgValue);\n");
    fprintf(f, "\t\tbreak;\n");
    fprintf(f, "\t}\n");

    fprintf(f, "}\n");

    fclose(f);
}

static void
printUsage() {
    printf("Usage: YapCodeGen client/server <basename> <defsfile>\n");
}


int main(int argc, char** argv)
{
    if (argc < 4) {
        printUsage();
        return -1;
    }

    bool clientCodeGen = false;

    if (strcasecmp(argv[1], "client") == 0) {
        printf("Client side code generation\n");
        clientCodeGen = true;
    }
    else if (strcasecmp(argv[1], "server") == 0) {
        printf("Server side code generation\n");
        clientCodeGen = false;
    }
    else {
        printUsage();
        return -1;
    }

    QString className(argv[2]);
    className += clientCodeGen ? "ClientBase" : "ServerBase";

    FILE* inputFile = fopen(argv[3], "r");
    if (!inputFile) {
        fprintf(stderr, "Failed to open defsfile: %s\n", argv[2]);
        return -1;
    }

    int lineNum = 0;
    while (!feof(inputFile)) {

        char*   line = NULL;
        size_t  lineSize;

        if (getline(&line, &lineSize, inputFile) == -1) {
            if (feof(inputFile))
                break;
            fprintf(stderr, "Failed to read line number: %d\n", lineNum + 1);
            return -1;
        }
        lineNum++;

        if (ignoreLine(line))
            continue;

        QString     lineQStr(line);
        QStringList argsQStrList = lineQStr.split(";", QString::KeepEmptyParts);

        if (argsQStrList.size() < 3) {
            fprintf(stderr, "Error parsing at line number: %d: %s", lineNum, line);
            return -1;
        }


        QString type = argsQStrList.at(0);
        if (type == "sync") {
            // need at least 4 args for sync cmd
            if (argsQStrList.size() < 4) {
                fprintf(stderr, "Error parsing sync cmd at line number: %d: %s", lineNum, line);
                return -1;
            }

            YapSyncCmd y;

            if (!parseCmdOrMsgAndValuePair(argsQStrList.at(1), y.cmd, y.cmdValue)) {
                fprintf(stderr, "Error parsing sync cmd  at line number: %d: %s", lineNum, line);
                return -1;
            }

            if (!parseTypeArgPairList(argsQStrList.at(2), y.inArgs)) {
                fprintf(stderr, "Error parsing sync cmd in args at line number: %d: %s", lineNum, line);
                return -1;
            }

            if (!parseTypeArgPairList(argsQStrList.at(3), y.outArgs)) {
                fprintf(stderr, "Error parsing sync cmd in args at line number: %d: %s", lineNum, line);
                return -1;
            }

            gSyncCmdList.append(y);
        }
        else if (type == "async") {

            YapAsyncCmd y;

            if (!parseCmdOrMsgAndValuePair(argsQStrList.at(1), y.cmd, y.cmdValue)) {
                fprintf(stderr, "Error parsing sync cmd  at line number: %d: %s", lineNum, line);
                return -1;
            }

            if (!parseTypeArgPairList(argsQStrList.at(2), y.inArgs)) {
                fprintf(stderr, "Error parsing sync cmd in args at line number: %d: %s", lineNum, line);
                return -1;
            }

            gAsyncCmdList.append(y);
        }
        else if (type == "msg") {

            YapMsg y;

            if (!parseCmdOrMsgAndValuePair(argsQStrList.at(1), y.msg, y.msgValue)) {
                fprintf(stderr, "Error parsing sync cmd  at line number: %d: %s", lineNum, line);
                return -1;
            }

            if (!parseTypeArgPairList(argsQStrList.at(2), y.inArgs)) {
                fprintf(stderr, "Error parsing sync cmd in args at line number: %d: %s", lineNum, line);
                return -1;
            }

            gMsgList.append(y);
        }
        else {
            fprintf(stderr, "Error parsing at line number: %d: %s", lineNum, line);
            return -1;
        }

        free(line);
    }

    fclose(inputFile);

    printf("Parsed %d sync commands, %d async command and %d messages\n",
           gSyncCmdList.size(), gAsyncCmdList.size(), gMsgList.size());

    if (clientCodeGen) {
        writeClientHeaderFile(className);
        printf("Wrote client header file: %s\n", (className + ".h").toLocal8Bit().constData());

        writeClientCppFile(className);
        printf("Wrote client cpp file: %s\n", (className + ".cpp").toLocal8Bit().constData());
    }
    else {
        writeServerHeaderFile(className);
        printf("Wrote server header file: %s\n", (className + ".h").toLocal8Bit().constData());

        writeServerCppFile(className);
        printf("Wrote server cpp file: %s\n", (className + ".cpp").toLocal8Bit().constData());
    }

    return 0;
}

