#include <QApplication>
#include <QFileInfo>
#include <QFrame>
#include <QTimer>
#include <cstdio>
int main(int argc, char **argv) {
    const QString socket = qEnvironmentVariable("LINKPAD_SOCKET");
    if (socket.isEmpty()) { std::fprintf(stderr,"Set LINKPAD_SOCKET for the lifecycle test\n"); return 1; }
    for (int i=0; i<3; ++i) {
        {
            QApplication app(argc,argv);
            QFrame root; root.show();
            QTimer::singleShot(30, &app, [&] {
                if (!QFileInfo::exists(socket)) {
                    std::fprintf(stderr,"Extension did not start its native listener\n");
                    app.exit(2);
                } else app.quit();
            });
            if (app.exec()) return 1;
        }
        if (QFileInfo::exists(socket)) {
            std::fprintf(stderr,"Listener path remained after Qt teardown\n"); return 1;
        }
    }
    std::puts("PASS three extension startup/teardown cycles");
}
