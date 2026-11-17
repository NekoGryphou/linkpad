#include "action.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#define CHECK(x) do { if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;} } while(0)
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);QTemporaryDir tmp;CHECK(tmp.isValid());
    const QString config=tmp.path()+"/action.json";
    auto write=[&](const QByteArray &data){QFile f(config);if(!f.open(QIODevice::WriteOnly))return false;return f.write(data)==data.size();};
    Action action;QString error;
    CHECK(!readAction(config,action,error));
    for(const QByteArray &bad:{QByteArray("{}"),QByteArray("{bad"),QByteArray("{\"command\":[]}"),QByteArray("{\"command\":[\"/bin/true\",2]}"),QByteArray("{\"command\":[\"bad\\u0000path\"]}")}) {
        CHECK(write(bad));CHECK(!readAction(config,action,error));CHECK(action.command.isEmpty());
    }
    CHECK(write(QByteArray(17000,'x')));CHECK(!readAction(config,action,error));
    const QString output=tmp.path()+"/result.txt";
    const QString program=QCoreApplication::applicationDirPath()+"/command-target";
    QJsonArray command;command.append(program);command.append(output);command.append("two words");command.append("$(not a shell);literal");
    QJsonObject json;json.insert("command",command);
    CHECK(write(QJsonDocument(json).toJson()));CHECK(readAction(config,action,error));
    CHECK(action.command.size()==4);
    for(const char *key:{"LD_PRELOAD","LD_LIBRARY_PATH","QT_PLUGIN_PATH","QT_QPA_PLATFORM","PATH","TMPDIR"}) {
        qputenv((QByteArray("STEAMLINK_ORIGINAL_")+key).constData(),QByteArray());
    }
    qputenv("STEAMLINK_ORIGINAL_PATH","/original/path");
    qputenv("STEAMLINK_ORIGINAL_QT_QPA_PLATFORM","wayland");
    qputenv("LD_PRELOAD","unwanted-steamlink-hook.so");
    qputenv("LD_LIBRARY_PATH","/unwanted/bundled/libraries");
    qputenv("QT_PLUGIN_PATH","/unwanted/plugins");
    qint64 pid=0;CHECK(launchActionCommand(action,&pid,error));CHECK(pid>0);
    QElapsedTimer timer;timer.start();QFile result(output);QByteArray data;
    while(timer.elapsed()<3000){if(result.open(QIODevice::ReadOnly)){data=result.readAll();result.close();if(data.contains("ARG=$(not a shell);literal\n"))break;}QThread::msleep(10);}
    CHECK(data.contains("ARG=two words\nARG=$(not a shell);literal\n"));
    CHECK(data.contains("LD_PRELOAD=\nLD_LIBRARY_PATH=\nQT_PLUGIN_PATH=\nQT_QPA_PLATFORM=wayland\nPATH=/original/path\nTMPDIR=\n"));
    action.command = QStringList() << QStringLiteral("/definitely/not/an/executable");CHECK(!launchActionCommand(action,&pid,error));
    const QString bin = tmp.path()+"/commands";CHECK(QDir().mkpath(bin));
    const QString bare = "only-on-restored-path";CHECK(QFile::copy(program,bin+"/"+bare));
    Action invalid = action; invalid.command = QStringList() << program << (tmp.path()+"/should-not-run");
    invalid.workingDirectory = tmp.path()+"/missing-directory";
    const bool invalidCwdStarted = launchActionCommand(invalid,&pid,error);
    if(invalidCwdStarted)std::fprintf(stderr,"Invalid working directory was accepted\n");
    action.command = QStringList() << bare << (tmp.path()+"/path-result") << "from restored PATH";
    qputenv("STEAMLINK_ORIGINAL_PATH",bin.toUtf8());qputenv("PATH","/wrong/parent/path");
    const bool bareStarted = launchActionCommand(action,&pid,error);
    if(!bareStarted)std::fprintf(stderr,"Executable on restored PATH was not found\n");
    CHECK(!invalidCwdStarted && bareStarted);
    QFile pathResult(tmp.path()+"/path-result");timer.restart();data.clear();
    while(timer.elapsed()<3000){if(pathResult.open(QIODevice::ReadOnly)){data=pathResult.readAll();pathResult.close();if(data.contains("ARG=from restored PATH\n"))break;}QThread::msleep(10);}
    CHECK(data.contains("ARG=from restored PATH\n"));
    std::puts("PASS configurable actions, argument boundaries, validation and clean child environment");
}
