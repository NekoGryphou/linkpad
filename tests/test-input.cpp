// Virtual-controller integration test. Runs inside a fake Steam Link home
// screen with native-test.so preloaded; needs /dev/uinput access.
#include <QApplication>
#include <QFrame>
#include <QLabel>
#include <QMenu>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <dlfcn.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

struct VirtualPad {
    int fd = -1;

    bool create(const char *name) {
        fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) return false;
        if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0) return false;
        for (int key : {BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_SELECT, BTN_START, BTN_MODE, BTN_TL, BTN_TR,
                        BTN_THUMBL, BTN_THUMBR}) {
            if (ioctl(fd, UI_SET_KEYBIT, key) < 0) return false;
        }
        uinput_setup setup = {};
        setup.id.bustype = BUS_USB;
        setup.id.vendor = 0x045e;
        setup.id.product = 0x02ea;
        std::snprintf(setup.name, sizeof setup.name, "%s", name);
        return ioctl(fd, UI_DEV_SETUP, &setup) == 0 && ioctl(fd, UI_DEV_CREATE) == 0;
    }

    void destroy() {
        if (fd < 0) return;
        ioctl(fd, UI_DEV_DESTROY);
        close(fd);
        fd = -1;
    }

    void key(int code, bool down) {
        input_event event = {};
        event.type = EV_KEY;
        event.code = static_cast<__u16>(code);
        event.value = down;
        if (write(fd, &event, sizeof event) < 0) std::perror("key");
        event.type = EV_SYN;
        event.code = SYN_REPORT;
        event.value = 0;
        if (write(fd, &event, sizeof event) < 0) std::perror("sync");
    }

    ~VirtualPad() { destroy(); }
};

static std::string slurp(const std::string &path) {
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

static bool exists(const std::string &path) { return access(path.c_str(), F_OK) == 0; }

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const char *configEnv = std::getenv("STEAMLINK_TOOL_CONFIG");
    const char *moduleEnv = std::getenv("STEAMLINK_TOOL_MODULE");
    if (!configEnv || !moduleEnv) {
        std::fprintf(stderr, "Run through scripts/test-input\n");
        return 1;
    }
    void *module = dlopen(moduleEnv, RTLD_NOW | RTLD_NOLOAD);
    auto pauseInput = module ? reinterpret_cast<void (*)(bool)>(dlsym(module, "steamlink_test_pause_input")) : nullptr;
    auto activations = module ? reinterpret_cast<unsigned (*)()>(dlsym(module, "steamlink_test_activations")) : nullptr;
    if (!pauseInput || !activations) {
        std::fprintf(stderr, "native-test.so test hooks not loaded\n");
        return 1;
    }

    const std::string config = configEnv;
    const std::string dir = config.substr(0, config.rfind('/'));
    const std::string target = QCoreApplication::applicationDirPath().toStdString() + "/command-target";
    auto out = [&](const char *name) { return dir + "/" + name; };
    auto command = [&](const char *name, const char *argument) {
        return "[\"" + target + "\",\"" + out(name) + "\",\"" + argument + "\"]";
    };
    auto writeConfig = [&](const std::string &text) {
        std::ofstream file(config, std::ios::trunc);
        file << text;
        return static_cast<bool>(file);
    };
    auto standardConfig = [&](const char *argument) {
        return "{\"shortcuts\":["
               "{\"buttons\":[\"view\",\"menu\"],\"hold_ms\":300,\"commands\":[" + command("s1a", argument) + "," +
               command("s1b", "second") + "]},"
               "{\"buttons\":[\"view\",\"x\"],\"hold_ms\":300,\"commands\":[" + command("s2", "x") + "]},"
               "{\"buttons\":[\"view\",\"menu\",\"a\"],\"hold_ms\":300,\"commands\":[" + command("s3", "a") + "]},"
               "{\"buttons\":[\"lb\",\"rb\"],\"hold_ms\":300,\"commands\":[" + command("p1", "1") +
               ",[\"/missing/command\"]," + command("p3", "3") + "]}]}";
    };
    auto clearOutputs = [&] {
        for (const char *name : {"s1a", "s1b", "s2", "s3", "p1", "p3"}) unlink(out(name).c_str());
    };

    QFrame root;
    QVBoxLayout layout(&root);
    QLabel title("Steam Link"), description("Stream games from your computer with Steam.");
    QToolButton play;
    play.setText("Start Playing");
    layout.addWidget(&title);
    layout.addWidget(&description);
    layout.addWidget(&play);
    QMenu menu(&root);
    menu.addAction("Foreign popup");
    menu.setAttribute(Qt::WA_ShowWithoutActivating);

    VirtualPad one, two;
    if (!one.create("Steam Link test controller 1") || !two.create("Steam Link test controller 2")) {
        std::perror("uinput");
        return 1;
    }
    if (!writeConfig(standardConfig("one"))) return 1;
    root.show();
    root.activateWindow();

    // Each step runs after its delay; a false result fails the test.
    struct Step {
        int delay;
        std::string name;
        std::function<bool()> run;
    };
    std::vector<Step> steps;
    auto at = [&](int delay, std::string name, std::function<bool()> run) {
        steps.push_back(Step{delay, std::move(name), std::move(run)});
    };
    auto act = [&](int delay, std::function<void()> run) {
        steps.push_back(Step{delay, "action", [run] { run(); return true; }});
    };
    auto hold = [&](VirtualPad &pad, std::initializer_list<int> keys) {
        for (int key : keys) pad.key(key, true);
    };
    auto release = [&](VirtualPad &pad, std::initializer_list<int> keys) {
        for (int key : keys) pad.key(key, false);
    };
    auto contains = [&](const char *name, const char *text) { return slurp(out(name)).find(text) != std::string::npos; };
    auto none = [&] {
        for (const char *name : {"s1a", "s1b", "s2", "s3", "p1", "p3"}) {
            if (exists(out(name))) return false;
        }
        return true;
    };
    const int fire = 800;  // comfortably after a 300 ms hold

    // 1. Two commands from one shortcut.
    act(600, [&] { hold(one, {BTN_SELECT, BTN_START}); });
    at(fire, "view+menu starts both commands", [&] {
        return contains("s1a", "ARG=one\n") && contains("s1b", "ARG=second\n") && !exists(out("s3")) && activations() == 1;
    });
    act(0, [&] { release(one, {BTN_SELECT, BTN_START}); clearOutputs(); });

    // 2. A second shortcut.
    act(200, [&] { hold(one, {BTN_SELECT, BTN_NORTH}); });
    at(fire, "view+x starts only its command", [&] { return contains("s2", "ARG=x\n") && !exists(out("s1a")); });
    act(0, [&] { release(one, {BTN_SELECT, BTN_NORTH}); clearOutputs(); });

    // 3. Overlap: the larger combination wins; releasing its extra button never falls back.
    act(200, [&] { hold(one, {BTN_SELECT, BTN_START, BTN_SOUTH}); });
    at(fire, "view+menu+a starts only the larger shortcut", [&] { return contains("s3", "ARG=a\n") && !exists(out("s1a")); });
    act(0, [&] { release(one, {BTN_SELECT, BTN_START, BTN_SOUTH}); clearOutputs(); });
    act(200, [&] { hold(one, {BTN_SELECT, BTN_START}); });
    act(100, [&] { hold(one, {BTN_SOUTH}); });
    act(100, [&] { release(one, {BTN_SOUTH}); });
    at(fire, "partial release of a larger combination starts nothing", none);
    act(0, [&] { release(one, {BTN_SELECT, BTN_START}); });

    // 4. A combination split across two controllers.
    act(200, [&] { hold(one, {BTN_SELECT}); hold(two, {BTN_START}); });
    at(fire, "buttons on different controllers start nothing", none);
    act(0, [&] { release(one, {BTN_SELECT}); release(two, {BTN_START}); });

    // 5. A foreign popup during the hold.
    act(200, [&] { hold(one, {BTN_SELECT, BTN_START}); });
    act(100, [&] { menu.popup(root.mapToGlobal(QPoint(20, 20))); });
    act(100, [&] { menu.hide(); root.activateWindow(); });
    at(fire, "a popup during the hold cancels it", none);
    act(0, [&] { release(one, {BTN_SELECT, BTN_START}); });

    // 6. Configuration reload without restart.
    act(200, [&] { writeConfig(standardConfig("reloaded")); });
    act(200, [&] { hold(one, {BTN_SELECT, BTN_START}); });
    at(fire, "edited configuration applies on the next press", [&] { return contains("s1a", "ARG=reloaded\n"); });
    act(0, [&] { release(one, {BTN_SELECT, BTN_START}); clearOutputs(); });

    // 7. Invalid configuration disables shortcuts until fixed.
    act(200, [&] { writeConfig("{bad"); });
    act(200, [&] { hold(one, {BTN_SELECT, BTN_START}); });
    at(fire, "invalid configuration starts nothing", none);
    act(0, [&] { release(one, {BTN_SELECT, BTN_START}); writeConfig(standardConfig("fixed")); });
    act(200, [&] { hold(one, {BTN_SELECT, BTN_START}); });
    at(fire, "fixed configuration works again", [&] { return contains("s1a", "ARG=fixed\n"); });
    act(0, [&] { release(one, {BTN_SELECT, BTN_START}); clearOutputs(); });

    // 8. Release and re-press queued before the old deadline: the deadline must not fire.
    unsigned before = 0;
    act(200, [&] { before = activations(); hold(one, {BTN_SELECT, BTN_START}); });
    act(50, [&] { pauseInput(true); });
    act(50, [&] { release(one, {BTN_SELECT, BTN_START}); });
    act(50, [&] { hold(one, {BTN_SELECT, BTN_START}); });
    at(250, "old deadline ignores a queued release and re-press", [&] { return none() && activations() == before; });
    act(0, [&] { pauseInput(false); });
    at(fire, "the re-press then counts as a new hold", [&] { return contains("s1a", "ARG=fixed\n"); });
    act(0, [&] { release(one, {BTN_SELECT, BTN_START}); clearOutputs(); });

    // 9. Controller disconnected mid-hold, then reconnected.
    act(200, [&] { hold(two, {BTN_SELECT, BTN_START}); });
    act(100, [&] { two.destroy(); });
    at(fire, "disconnect mid-hold starts nothing", none);
    act(0, [&] {
        if (!two.create("Steam Link test controller 2")) std::perror("uinput");
    });
    act(800, [&] { hold(two, {BTN_SELECT, BTN_START}); });
    at(fire, "reconnected controller works", [&] { return contains("s1a", "ARG=fixed\n"); });
    act(0, [&] { release(two, {BTN_SELECT, BTN_START}); clearOutputs(); });

    // 10. A failing command does not stop the others.
    act(200, [&] { hold(one, {BTN_TL, BTN_TR}); });
    at(fire, "commands after a failed one still start", [&] { return contains("p1", "ARG=1\n") && contains("p3", "ARG=3\n"); });
    act(0, [&] { release(one, {BTN_TL, BTN_TR}); clearOutputs(); });

    std::size_t index = 0;
    std::function<void()> next = [&] {
        if (index == steps.size()) {
            std::puts("PASS multiple shortcuts and commands, overlap, controllers, popup, reload, invalid config, "
                      "queued input, disconnect and partial failure");
            app.quit();
            return;
        }
        QTimer::singleShot(steps[index].delay, &app, [&] {
            if (!steps[index].run()) {
                std::fprintf(stderr, "FAIL %s\n", steps[index].name.c_str());
                app.exit(1);
                return;
            }
            ++index;
            next();
        });
    };
    next();
    return app.exec();
}
