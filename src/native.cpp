#include "chord.hpp"
#include "home.hpp"
#include "action.hpp"
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QSocketNotifier>
#include <QTimer>
#include <QThread>
#ifdef NATIVE_DIAGNOSTICS
#include <QMouseEvent>
#include <QKeyEvent>
#endif
#include <linux/input.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <functional>
#include <map>

static const QEvent *nativeEvent = nullptr;
static int nativeButton = 0;
extern "C" void steamlink_native_button(void *event, int button, bool) {
    if (!qApp || QThread::currentThread() != qApp->thread()) return;
    nativeEvent = static_cast<QEvent *>(event); nativeButton = button;
}

// SockAct is delivered by Qt's existing dispatcher; no polling thread or timer.
class Readable final : public QSocketNotifier {
    std::function<void()> ready;
public:
    Readable(int fd, QObject *parent, std::function<void()> callback)
        : QSocketNotifier(fd, QSocketNotifier::Read, parent), ready(std::move(callback)) {}
    bool event(QEvent *e) override {
        if (e->type() == QEvent::SockAct) { ready(); return true; }
        return QSocketNotifier::event(e);
    }
};

struct Pad : QObject {
    int fd;
    Chord chord;
    QPointer<QWidget> startedOn;
    bool dropped = false;
    QTimer hold;
    explicit Pad(int f, QObject *parent) : QObject(parent), fd(f) { hold.setSingleShot(true); hold.setTimerType(Qt::PreciseTimer); }
    ~Pad() override {
        for (auto *n : findChildren<QSocketNotifier *>()) n->setEnabled(false);
        close(fd);
    }
};

class Shortcut final : public QObject {
    std::map<QString, Pad *> pads;
    int notifyFd = -1;
    QElapsedTimer clock;
    QLocalServer server;
    QPointer<QWidget> homeButton;
    QPointer<QWidget> watchedHome;
    unsigned activations = 0;
    unsigned checks = 0;
    bool home() {
        ++checks;
        QWidget *candidate = nativeHomeButton();
        if (candidate && candidate != watchedHome.data()) {
            connect(candidate, &QObject::destroyed, this, [this] { cancel(); });
            watchedHome = candidate;
        }
        homeButton = candidate;
        if (!candidate) cancel();
        if (homeButton) {
            homeButton->installEventFilter(this);
            homeButton->window()->installEventFilter(this);
        }
        return !homeButton.isNull();
    }
    void cancel() {
        for (auto &entry : pads) { entry.second->chord.block(); entry.second->hold.stop(); }
    }
    bool physicallyHeld(int code) {
        for (auto &entry : pads) {
            unsigned long keys[(KEY_MAX + 8*sizeof(unsigned long)) / (8*sizeof(unsigned long))] = {};
            if (ioctl(entry.second->fd, EVIOCGKEY(sizeof keys), keys) >= 0 && bit(keys,code)) return true;
        }
        return false;
    }
    static bool bit(const unsigned long *bits, int code) {
        constexpr unsigned n = sizeof(unsigned long) * 8;
        return (bits[code / n] >> (code % n)) & 1;
    }
    bool sync(Pad *p) {
        unsigned long keys[(KEY_MAX + 8*sizeof(unsigned long)) / (8*sizeof(unsigned long))] = {};
        if (ioctl(p->fd, EVIOCGKEY(sizeof keys), keys) < 0) return false;
        p->chord.reset(bit(keys, BTN_SELECT), bit(keys, BTN_START));
        p->hold.stop(); return true;
    }
    void activate() {
        if (!home()) return;
        cancel();
        Action action; QString error;
        if (!readAction(actionConfigPath(), action, error)) {
            std::fprintf(stderr,"STEAMLINK_SHORTCUT action_config_error=%s\n",error.toUtf8().constData());
            return;
        }
        qint64 pid = 0;
        if (launchActionCommand(action, &pid, error)) {
            ++activations;
            std::fprintf(stderr,"STEAMLINK_SHORTCUT command_started pid=%lld\n",static_cast<long long>(pid));
        } else std::fprintf(stderr,"STEAMLINK_SHORTCUT command_failed=%s\n",error.toUtf8().constData());
    }
    void remove(const QString &path) {
        auto it = pads.find(path);
        if (it == pads.end()) return;
        Pad *p = it->second; pads.erase(it);
        p->hold.stop();
        for (auto *n : p->findChildren<QSocketNotifier *>()) n->setEnabled(false);
        p->deleteLater();
    }
    void drain(const QString &path, Pad *p) {
        input_event events[32];
        for (;;) {
            ssize_t n = read(p->fd, events, sizeof events);
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && errno == EAGAIN) return;
            if (n <= 0 || n % sizeof(input_event)) { remove(path); return; }
            for (ssize_t i = 0; i < n / static_cast<ssize_t>(sizeof(input_event)); ++i) {
                const auto &e = events[i];
                if (e.type == EV_SYN && e.code == SYN_DROPPED) {
                    p->dropped = true; p->chord.block(); p->hold.stop(); continue;
                }
                if (p->dropped) {
                    if (e.type == EV_SYN && e.code == SYN_REPORT) {
                        if (!sync(p)) { remove(path); return; }
                        p->dropped = false;
                    }
                    continue;
                }
                if (e.type != EV_KEY || (e.code != BTN_SELECT && e.code != BTN_START) || e.value == 2) continue;
                bool start = p->chord.update(e.code, e.value != 0, home(), clock.elapsed());
                if (!p->chord.pending) p->hold.stop();
                if (start) { p->startedOn = homeButton; p->hold.start(1000); }
            }
        }
    }
    void discover() {
        const QDir directory("/dev/input");
        const QStringList names = directory.entryList({"event*"}, QDir::Files | QDir::System);
        for (auto it = pads.begin(); it != pads.end();) {
            const QString path = it->first;
            struct stat opened = {}, current = {};
            const bool same = fstat(it->second->fd, &opened) == 0 &&
                stat(path.toLocal8Bit().constData(), &current) == 0 &&
                opened.st_dev == current.st_dev && opened.st_ino == current.st_ino;
            ++it;
            if (!same) remove(path);
        }
        for (const QString &name : names) {
            QString path = directory.filePath(name);
            if (pads.count(path)) continue;
            int fd = open(path.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0) continue;
            input_id id = {};
            unsigned long keys[(KEY_MAX + 8*sizeof(unsigned long)) / (8*sizeof(unsigned long))] = {};
            if (ioctl(fd, EVIOCGID, &id) < 0 || id.vendor != 0x045e ||
                ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) < 0 ||
                !bit(keys, BTN_SOUTH) || !bit(keys, BTN_SELECT) || !bit(keys, BTN_START)) { close(fd); continue; }
            auto *p = new Pad(fd, this);
            if (!sync(p)) { delete p; continue; }
            pads.emplace(path, p);
            new Readable(fd, p, [this,path,p] { drain(path,p); });
            connect(&p->hold, &QTimer::timeout, p, [this,p] {
                const bool eligible = home() && p->startedOn && p->startedOn == homeButton;
                if (p->chord.due(eligible, clock.elapsed())) activate();
                else if (p->chord.pending) p->hold.start(static_cast<int>(p->chord.deadline - clock.elapsed() + 1));
            });
            std::fprintf(stderr,"STEAMLINK_SHORTCUT controller=%s\n",path.toLocal8Bit().constData());
        }
    }
public:
    Shortcut() : QObject(qApp) {
        clock.start();
        nativeEvent = nullptr;
        qApp->installEventFilter(this);
        notifyFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (notifyFd >= 0) {
            // /dev/input can appear only after the first controller connects.
            inotify_add_watch(notifyFd, "/dev", IN_CREATE | IN_MOVED_TO);
            inotify_add_watch(notifyFd, "/dev/input", IN_CREATE | IN_DELETE | IN_ATTRIB | IN_MOVED_TO | IN_MOVED_FROM | IN_DELETE_SELF);
            new Readable(notifyFd, this, [this] {
                alignas(inotify_event) char buffer[4096];
                while (read(notifyFd, buffer, sizeof buffer) > 0) {}
                inotify_add_watch(notifyFd, "/dev/input", IN_CREATE | IN_DELETE | IN_ATTRIB | IN_MOVED_TO | IN_MOVED_FROM | IN_DELETE_SELF);
                discover();
            });
        }
        discover();
        const QString path = qEnvironmentVariable("STEAMLINK_SHORTCUT_SOCKET");
        if (!path.isEmpty()) {
            QLocalServer::removeServer(path);
            server.setSocketOptions(QLocalServer::UserAccessOption);
            if (server.listen(path)) connect(&server, &QLocalServer::newConnection, this, [this] {
                while (server.hasPendingConnections()) {
                    auto *s = server.nextPendingConnection();
                    connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
                    QTimer::singleShot(2000, s, [s] { s->abort(); s->deleteLater(); });
                    connect(s, &QLocalSocket::readyRead, this, [this,s] {
                        if (s->bytesAvailable() > 128) { s->abort(); return; }
                        if (!s->canReadLine()) return;
                        QByteArray command = s->readLine().trimmed();
                        if (command == "state") {
                            bool eligible = home();
                            s->write(QString("home=%1 activations=%2 controllers=%3 checks=%4\n")
                                .arg(eligible).arg(activations).arg(pads.size()).arg(checks).toUtf8());
                        }
#ifdef NATIVE_DIAGNOSTICS
                        else if (command.startsWith("click ")) {
                            int x,y;
                            if (std::sscanf(command.constData(),"click %d %d",&x,&y)==2) {
                                QPoint global(x,y); QPointer<QWidget> w = QApplication::widgetAt(global);
                                if (w) { QMouseEvent press(QEvent::MouseButtonPress,w->mapFromGlobal(global),w->window()->mapFromGlobal(global),global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(w,&press); }
                                if (w) { QMouseEvent release(QEvent::MouseButtonRelease,w->mapFromGlobal(global),w->window()->mapFromGlobal(global),global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier); QApplication::sendEvent(w,&release); }
                            }
                            s->write("OK\n");
                        }
#endif
                        else s->write("UNKNOWN\n");
                        s->flush(); s->disconnectFromServer();
                    });
                }
            });
        }
        std::fprintf(stderr,"STEAMLINK_SHORTCUT ready native-events-only\n");
    }
    ~Shortcut() override {
        // QApplication clears qApp before QObject deletes its children.
        // QObject automatically removes this filter when it destroys us.
        nativeEvent = nullptr;
        for (auto *n : findChildren<QSocketNotifier *>()) n->setEnabled(false);
        if (notifyFd >= 0) close(notifyFd);
    }
    bool eventFilter(QObject *target, QEvent *event) override {
        if (event->type() == QEvent::WindowBlocked) cancel();
        if (event->type() == QEvent::Show) {
            auto *widget = qobject_cast<QWidget *>(target);
            if (widget &&
                (widget->isModal() || widget->windowType() == Qt::Popup)) cancel();
        }
        // Valve's Qt emits QControllerEvent (0x300). Its Back event also
        // represents B, so verify the actual View key before consuming it.
        if (event == nativeEvent) {
            const int button = nativeButton;
            nativeEvent = nullptr;
            if (int(event->type()) == 0x300 && (button == 2 || button == 7) &&
                home() && physicallyHeld(button == 2 ? BTN_SELECT : BTN_START)) {
                event->accept(); return true;
            }
        }
#ifdef NATIVE_DIAGNOSTICS
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
            std::fprintf(stderr,"NATIVE_KEY target=%s type=%d key=%d\n",target->metaObject()->className(),int(event->type()),static_cast<QKeyEvent *>(event)->key());
        if (event->type() == QEvent::Close || event->type() == QEvent::Quit)
            std::fprintf(stderr,"NATIVE_CLOSE target=%s type=%d\n",target->metaObject()->className(),int(event->type()));
#endif
        if (homeButton && ((target == homeButton && event->type() == QEvent::Hide) ||
            (target == homeButton->window() && event->type() == QEvent::WindowDeactivate))) cancel();
        return false;
    }
};

extern "C" void steamlink_probe_start() {
    try { new Shortcut; }
    catch (...) { std::fprintf(stderr,"STEAMLINK_SHORTCUT disabled: initialization exception\n"); }
}
