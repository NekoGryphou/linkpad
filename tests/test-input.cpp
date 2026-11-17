#include <QApplication>
#include <QFrame>
#include <QLabel>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
struct Pad {
    int fd=-1;
    bool open() {
        fd=::open("/dev/uinput",O_WRONLY|O_NONBLOCK);
        if(fd<0)return false;
        if(ioctl(fd,UI_SET_EVBIT,EV_KEY)<0)return false;
        for(int key:{BTN_SOUTH,BTN_SELECT,BTN_START})if(ioctl(fd,UI_SET_KEYBIT,key)<0)return false;
        uinput_setup setup={};setup.id.bustype=BUS_USB;setup.id.vendor=0x045e;setup.id.product=0x02ea;
        std::strcpy(setup.name,"Steam Link shortcut test controller");
        return ioctl(fd,UI_DEV_SETUP,&setup)==0&&ioctl(fd,UI_DEV_CREATE)==0;
    }
    void key(int code,int value) {
        input_event event={};event.type=EV_KEY;event.code=code;event.value=value;
        if(write(fd,&event,sizeof event)<0)std::perror("key");
        event.type=EV_SYN;event.code=SYN_REPORT;event.value=0;
        if(write(fd,&event,sizeof event)<0)std::perror("sync");
    }
    void chord(bool down){key(BTN_SELECT,down);key(BTN_START,down);}
    ~Pad(){if(fd>=0){ioctl(fd,UI_DEV_DESTROY);close(fd);}}
};
int main(int argc,char **argv) {
    QApplication app(argc,argv);Pad pad;
    if(!pad.open()){std::perror("uinput");return 1;}
    QFrame root;QVBoxLayout layout(&root);
    QLabel title("Steam Link"),description("Stream games from your computer with Steam.");
    QToolButton play;play.setText("Start Playing");
    layout.addWidget(&title);layout.addWidget(&description);layout.addWidget(&play);
    QMenu menu(&root);menu.addAction("Foreign popup");menu.setAttribute(Qt::WA_ShowWithoutActivating);
    const QString config=qEnvironmentVariable("STEAMLINK_TOOL_CONFIG");
    const QString output=QFileInfo(config).absolutePath()+"/command-result";
    auto configure=[&](const QString &argument) {
        QJsonArray command;command.append(QCoreApplication::applicationDirPath()+"/command-target");command.append(output);command.append(argument);
        QJsonObject action;action.insert("command",command);
        QFile file(config);return file.open(QIODevice::WriteOnly)&&file.write(QJsonDocument(action).toJson())>0;
    };
    auto contains=[&](const QByteArray &text){QFile f(output);return f.open(QIODevice::ReadOnly)&&f.readAll().contains(text);};
    if(!configure("first command"))return 1;
    root.show();root.activateWindow();
    QTimer::singleShot(250,&app,[&]{pad.chord(true);});
    QTimer::singleShot(1500,&app,[&]{pad.chord(false);if(!contains("ARG=first command\n")){app.exit(1);return;}QFile::remove(output);});
    QTimer::singleShot(2000,&app,[&]{pad.chord(true);});
    QTimer::singleShot(2500,&app,[&]{menu.popup(root.mapToGlobal(QPoint(20,20)));});
    QTimer::singleShot(2650,&app,[&]{menu.hide();root.activateWindow();});
    QTimer::singleShot(3400,&app,[&]{pad.chord(false);if(QFile::exists(output)||!configure("reloaded command")){app.exit(1);return;}});
    QTimer::singleShot(3700,&app,[&]{pad.chord(true);});
    QTimer::singleShot(5000,&app,[&]{pad.chord(false);if(!contains("ARG=reloaded command\n")){app.exit(1);return;}std::puts("PASS command activation, foreign-popup cancellation and configuration reload");app.quit();});
    return app.exec();
}
