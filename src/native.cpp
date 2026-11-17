// Steam Link extension loaded into the client by bootstrap.so. It reads Xbox
// controllers without grabbing them, matches configured button combinations
// per controller, and starts the configured commands on the exact home screen.
//
// Qt headers here are newer than Valve's bundled Qt 5.14.1 libraries, so only
// long-stable Qt classes are used; JSON, processes, time and directories are
// handled without Qt.
#include "config.hpp"
#include "chord.hpp"
#include "spawn.hpp"
#include "home.hpp"
#include "native-records.hpp"

#include <QApplication>
#include <QEvent>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QSocketNotifier>
#include <QTimer>
#include <QWidget>

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <exception>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int controllerEventType = 0x300;  // Valve's QControllerEvent

std::int64_t nowMs() {
    timespec now = {};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<std::int64_t>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
}

void report(const std::string &message) {
    std::fprintf(stderr, "LINKPAD %s\n", message.c_str());
}

// Exceptions must never unwind into Steam Link's event loop.
template <typename Function>
void guarded(const char *where, Function &&function) {
    try {
        function();
    } catch (const std::exception &error) {
        report(std::string(where) + " error=" + error.what());
    } catch (...) {
        report(std::string(where) + " error=unknown");
    }
}

NativeRecords nativeButtons;

// Uses QObject::event(SockAct) rather than the activated signal, whose
// signature differs between the Qt 5.15 headers and Qt 5.14 libraries.
class Readable final : public QSocketNotifier {
    std::function<void()> ready;

public:
    Readable(int fd, QObject *parent, std::function<void()> callback)
        : QSocketNotifier(fd, QSocketNotifier::Read, parent), ready(std::move(callback)) {}

    bool event(QEvent *e) override {
        if (e->type() == QEvent::SockAct) {
            ready();
            return true;
        }
        return QSocketNotifier::event(e);
    }
};

bool testBit(const unsigned long *bits, int code) {
    constexpr int width = sizeof(unsigned long) * 8;
    return (bits[code / width] >> (code % width)) & 1;
}

constexpr std::size_t keyWords = (KEY_MAX + 8 * sizeof(unsigned long)) / (8 * sizeof(unsigned long));

// Controller buttons the kernel currently reports as held.
bool heldButtons(int fd, std::set<int> &held) {
    unsigned long keys[keyWords] = {};
    if (ioctl(fd, EVIOCGKEY(sizeof keys), keys) < 0) return false;
    held.clear();
    for (int code = BTN_SOUTH; code <= BTN_THUMBR; ++code) {
        if (isControllerButton(code) && testBit(keys, code)) held.insert(code);
    }
    return true;
}

struct Pad final : QObject {
    std::string path;
    int fd;
    dev_t device;
    ino_t inode;
    Chord chord;
    QPointer<QWidget> startedOn;
    bool dropped = false;
    QTimer hold;
    Readable *reader = nullptr;

    Pad(QObject *parent, std::string devicePath, int descriptor, const struct stat &info)
        : QObject(parent), path(std::move(devicePath)), fd(descriptor), device(info.st_dev), inode(info.st_ino) {
        hold.setSingleShot(true);
        hold.setTimerType(Qt::PreciseTimer);
    }

    ~Pad() override {
        if (reader) reader->setEnabled(false);
        close(fd);
    }
};

struct ChildWatch final : QObject {
    pid_t pid;
    int fd;
    Readable *reader = nullptr;

    ChildWatch(QObject *parent, pid_t child, int descriptor) : QObject(parent), pid(child), fd(descriptor) {}

    ~ChildWatch() override {
        if (reader) reader->setEnabled(false);
        close(fd);
    }
};

class Shortcut final : public QObject {
    std::map<std::string, Pad *> pads;
    int notifyFd = -1;
    QLocalServer *server = nullptr;
    QPointer<QWidget> homeButton;
    std::vector<ShortcutConfig> shortcuts;
    std::vector<ChordRule> rules;
    std::string configKey;
    std::set<int> swallowed;
    unsigned activations = 0;
    unsigned checks = 0;

    bool home() {
        ++checks;
        homeButton = nativeHomeButton();
        if (!homeButton) cancel();
        return !homeButton.isNull();
    }

    void cancel() {
        for (auto &entry : pads) {
            entry.second->chord.block();
            entry.second->hold.stop();
        }
    }

    // Native presses belong to a shortcut from a full match until full release,
    // and on home while the held buttons could still become a combination:
    // Steam Link quits when View is pressed alone on its home screen.
    bool reserved() {
        bool candidate = false;
        for (const auto &entry : pads) {
            if (entry.second->chord.active()) return true;
            candidate |= entry.second->chord.candidate();
        }
        return candidate && home();
    }

    void schedule(Pad *p) {
        p->hold.start(static_cast<int>(std::max<std::int64_t>(1, p->chord.deadline - nowMs())));
    }

    // Reload when the configuration file's identity, size or times change.
    void reloadIfChanged(bool force) {
        const std::string path = configPath();
        struct stat info = {};
        std::string key;
        if (stat(path.c_str(), &info) == 0) {
            key = std::to_string(info.st_dev) + ":" + std::to_string(info.st_ino) + ":" + std::to_string(info.st_size) +
                  ":" + std::to_string(info.st_mtim.tv_sec) + "." + std::to_string(info.st_mtim.tv_nsec) + ":" +
                  std::to_string(info.st_ctim.tv_sec) + "." + std::to_string(info.st_ctim.tv_nsec);
        } else {
            key = std::string("missing:") + std::strerror(errno);
        }
        if (!force && key == configKey) return;
        configKey = key;

        std::vector<ShortcutConfig> loaded;
        std::string error;
        const bool valid = loadConfig(path, homeDirectory(), loaded, error);
        shortcuts = valid ? loaded : std::vector<ShortcutConfig>();
        rules.clear();
        for (const auto &shortcut : shortcuts) rules.push_back(ChordRule{shortcut.buttons, shortcut.holdMs});
        report(valid ? "config_loaded shortcuts=" + std::to_string(shortcuts.size()) : "config_error=" + error);
        // Buttons already processed as held stay blocked until released. The
        // kernel state is not used here: it already contains the press being
        // handled, which would block the combination the user is pressing now.
        for (auto &entry : pads) {
            Pad *p = entry.second;
            p->chord.configure(rules, p->chord.pressed);
            p->hold.stop();
        }
    }

    bool resync(Pad *p) {
        std::set<int> held;
        const bool ok = heldButtons(p->fd, held);
        p->chord.configure(rules, held);
        p->hold.stop();
        return ok;
    }

    void activate(int index) {
        if (index < 0 || static_cast<std::size_t>(index) >= shortcuts.size()) return;
        const ShortcutConfig shortcut = shortcuts[static_cast<std::size_t>(index)];
        cancel();
        ++activations;
        const std::vector<SpawnResult> results = spawnCommands(shortcut.commands, shortcut.workingDirectory);
        for (std::size_t i = 0; i < results.size(); ++i) {
            const std::string prefix = "shortcut=" + std::to_string(index + 1) + " command=" + std::to_string(i + 1);
            if (results[i].pid > 0) {
                report(prefix + " pid=" + std::to_string(results[i].pid));
                watchChild(results[i].pid);
            } else {
                report(prefix + " failed=" + results[i].error);
            }
        }
    }

    // Reap each started command when it exits, without a SIGCHLD handler.
    void watchChild(pid_t pid) {
        const int fd = static_cast<int>(syscall(SYS_pidfd_open, pid, 0));
        if (fd < 0) {
            report("pid=" + std::to_string(pid) + " not reaped: pidfd_open " + std::strerror(errno));
            return;
        }
        auto *watch = new ChildWatch(this, pid, fd);
        watch->reader = new Readable(fd, watch, [watch] {
            if (reapChild(watch->pid)) {
                watch->reader->setEnabled(false);
                watch->deleteLater();
            }
        });
    }

    void remove(Pad *p) {
        auto it = pads.find(p->path);
        if (it != pads.end() && it->second == p) pads.erase(it);
        p->hold.stop();
        if (p->reader) p->reader->setEnabled(false);
        p->deleteLater();
        report("controller_removed=" + p->path);
    }

    // Returns false when the pad was removed.
    bool handle(Pad *p, const input_event &e) {
        if (e.type == EV_SYN && e.code == SYN_DROPPED) {
            p->dropped = true;
            p->chord.block();
            p->hold.stop();
            return true;
        }
        if (p->dropped) {
            if (e.type == EV_SYN && e.code == SYN_REPORT) {
                if (!resync(p)) {
                    remove(p);
                    return false;
                }
                p->dropped = false;
            }
            return true;
        }
        if (e.type != EV_KEY || e.value == 2 || !isControllerButton(e.code)) return true;
        if (e.value) reloadIfChanged(false);
        if (p->chord.update(e.code, e.value != 0, home(), nowMs())) {
            p->startedOn = homeButton;
            schedule(p);
        } else if (!p->chord.pending) {
            p->hold.stop();
        }
        return true;
    }

    // Returns false when the pad was removed.
    bool drain(Pad *p) {
        input_event events[64];
        for (;;) {
            const ssize_t n = read(p->fd, events, sizeof events);
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && errno == EAGAIN) return true;
            if (n <= 0 || n % static_cast<ssize_t>(sizeof(input_event)) != 0) {
                remove(p);
                return false;
            }
            for (ssize_t i = 0; i < n / static_cast<ssize_t>(sizeof(input_event)); ++i) {
                if (!handle(p, events[i])) return false;
            }
        }
    }

    void drainAll() {
        std::vector<Pad *> current;
        for (auto &entry : pads) current.push_back(entry.second);
        for (Pad *p : current) {
            auto it = pads.find(p->path);
            if (it != pads.end() && it->second == p) drain(p);
        }
    }

    // Deadline: first apply input that is still queued, so an old deadline
    // cannot fire after a release and re-press that Qt has not delivered yet.
    void deadline(Pad *p) {
        if (!drain(p)) return;
        reloadIfChanged(false);
        if (!p->chord.pending) return;
        const bool eligible = home() && p->startedOn && p->startedOn == homeButton;
        const int match = p->chord.due(eligible, nowMs());
        if (match >= 0) {
            activate(match);
        } else if (p->chord.pending) {
            schedule(p);
        }
    }

    void discover() {
        for (auto it = pads.begin(); it != pads.end();) {
            Pad *p = it->second;
            ++it;
            struct stat current = {};
            if (stat(p->path.c_str(), &current) != 0 || current.st_dev != p->device || current.st_ino != p->inode)
                remove(p);
        }
        DIR *directory = opendir("/dev/input");
        if (!directory) return;
        std::vector<std::string> paths;
        while (dirent *entry = readdir(directory)) {
            if (std::strncmp(entry->d_name, "event", 5) == 0) paths.push_back(std::string("/dev/input/") + entry->d_name);
        }
        closedir(directory);
        std::sort(paths.begin(), paths.end());
        for (const std::string &path : paths) {
            if (pads.count(path)) continue;
            const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0) continue;
            input_id id = {};
            unsigned long keys[keyWords] = {};
            struct stat info = {};
            if (ioctl(fd, EVIOCGID, &id) < 0 || id.vendor != 0x045e ||
                ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) < 0 || !testBit(keys, BTN_SOUTH) ||
                !testBit(keys, BTN_SELECT) || !testBit(keys, BTN_START) || fstat(fd, &info) != 0) {
                close(fd);
                continue;
            }
            auto *p = new Pad(this, path, fd, info);
            if (!resync(p)) {
                delete p;
                continue;
            }
            pads.emplace(path, p);
            p->reader = new Readable(fd, p, [this, p] { guarded("input", [&] { drain(p); }); });
            connect(&p->hold, &QTimer::timeout, p, [this, p] { guarded("deadline", [&] { deadline(p); }); });
            report("controller=" + path);
        }
    }

    bool filterController(QEvent *event) {
        const std::vector<NativeButton> records = nativeButtons.take(event);
        if (records.empty()) return false;
        bool pressed = false;
        for (const auto &record : records) pressed |= record.down;
        if (pressed) drainAll();  // evdev may not have been read yet for this press
        const bool active = reserved();
        bool swallow = false;
        for (const auto &record : records) {
            if (record.down) {
                if (active) {
                    swallowed.insert(record.button);
                    swallow = true;
                } else {
                    swallowed.erase(record.button);
                }
            } else if (swallowed.erase(record.button)) {
                swallow = true;
            }
        }
        if (swallow) event->accept();
        return swallow;
    }

    void answer(QLocalSocket *socket) {
        if (socket->bytesAvailable() > 128) {
            socket->abort();
            return;
        }
        if (!socket->canReadLine()) return;
        const QByteArray command = socket->readLine().trimmed();
        if (command == "state") {
            const bool eligible = home();
            const std::string state = "home=" + std::to_string(eligible) + " activations=" + std::to_string(activations) +
                                      " controllers=" + std::to_string(pads.size()) +
                                      " shortcuts=" + std::to_string(shortcuts.size()) + "\n";
            socket->write(state.c_str());
        } else {
            socket->write("UNKNOWN\n");
        }
        socket->flush();
        socket->disconnectFromServer();
    }

    void listen(const char *path) {
        server = new QLocalServer(this);
        QLocalServer::removeServer(path);
        server->setSocketOptions(QLocalServer::UserAccessOption);
        if (!server->listen(path)) {
            report(std::string("socket_error=") + path);
            return;
        }
        connect(server, &QLocalServer::newConnection, this, [this] {
            while (server->hasPendingConnections()) {
                QLocalSocket *socket = server->nextPendingConnection();
                connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
                QTimer::singleShot(2000, socket, [socket] {
                    socket->abort();
                    socket->deleteLater();
                });
                connect(socket, &QLocalSocket::readyRead, this, [this, socket] { guarded("socket", [&] { answer(socket); }); });
            }
        });
    }

public:
    Shortcut() : QObject(qApp) {
        qApp->installEventFilter(this);
        reloadIfChanged(true);
        notifyFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (notifyFd >= 0) {
            // /dev/input can appear only after the first controller connects.
            const uint32_t inputEvents = IN_CREATE | IN_DELETE | IN_ATTRIB | IN_MOVED_TO | IN_MOVED_FROM | IN_DELETE_SELF;
            inotify_add_watch(notifyFd, "/dev", IN_CREATE | IN_MOVED_TO);
            inotify_add_watch(notifyFd, "/dev/input", inputEvents);
            new Readable(notifyFd, this, [this, inputEvents] {
                guarded("hotplug", [&] {
                    alignas(inotify_event) char buffer[4096];
                    while (read(notifyFd, buffer, sizeof buffer) > 0) {
                    }
                    inotify_add_watch(notifyFd, "/dev/input", inputEvents);
                    discover();
                });
            });
        }
        discover();
        const char *socket = std::getenv("LINKPAD_SOCKET");
        if (socket && *socket) listen(socket);
        report("ready");
    }

    ~Shortcut() override {
        // QApplication clears qApp before QObject deletes its children; the
        // application event filter is removed automatically.
        for (auto *notifier : findChildren<QSocketNotifier *>()) notifier->setEnabled(false);
        if (notifyFd >= 0) close(notifyFd);
    }

    bool eventFilter(QObject *target, QEvent *event) override {
        bool swallow = false;
        guarded("filter", [&] {
            const int type = static_cast<int>(event->type());
            if (type == controllerEventType) {
                swallow = filterController(event);
            } else if (type == QEvent::WindowBlocked) {
                cancel();
            } else if (type == QEvent::Show) {
                auto *widget = qobject_cast<QWidget *>(target);
                if (widget && (widget->isModal() || widget->windowType() == Qt::Popup)) cancel();
            } else if (homeButton && ((target == homeButton && type == QEvent::Hide) ||
                                      (target == homeButton->window() && type == QEvent::WindowDeactivate))) {
                cancel();
            }
        });
        return swallow;
    }

#ifdef LINKPAD_TEST_HOOKS
    // Lets the input test queue controller events that Qt has not delivered.
    void pauseInput(bool paused) {
        for (auto &entry : pads) entry.second->reader->setEnabled(!paused);
    }

    unsigned activationCount() const { return activations; }
#endif
};

QPointer<Shortcut> instance;

}  // namespace

extern "C" void linkpad_native_button(void *event, int button, bool down) noexcept {
    nativeButtons.record(event, button, down);
}

extern "C" void linkpad_start() {
    if (instance) return;  // QApplication::exec may run more than once
    guarded("start", [] { instance = new Shortcut; });
}

#ifdef LINKPAD_TEST_HOOKS
extern "C" void linkpad_test_pause_input(bool paused) {
    if (instance) instance->pauseInput(paused);
}

extern "C" unsigned linkpad_test_activations() {
    Shortcut *shortcut = instance.data();
    return shortcut ? shortcut->activationCount() : 0;
}
#endif
