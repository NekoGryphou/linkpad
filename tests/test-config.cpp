#include "config.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static const char *home = "/home/tester";

static bool rejects(const std::string &text, const char *mention = nullptr) {
    std::vector<ShortcutConfig> out(1);
    std::string error;
    const bool ok = parseConfig(text, home, out, error);
    if (ok || error.empty() || !out.empty()) {
        std::fprintf(stderr, "accepted or bad state: %s\n", text.c_str());
        return false;
    }
    if (mention && error.find(mention) == std::string::npos) {
        std::fprintf(stderr, "error '%s' does not mention '%s'\n", error.c_str(), mention);
        return false;
    }
    return true;
}

static std::string shortcut(const std::string &fields) {
    return "{\"shortcuts\":[{" + fields + "}]}";
}

static int test_example() {
    const std::string text = R"({
      "shortcuts": [
        {"buttons": ["view", "menu"], "hold_ms": 1000, "working_directory": "/srv",
         "commands": [["/usr/bin/foo", "two words", "$(x);y"], ["/usr/local/bin/baz"]]}
      ]
    })";
    std::vector<ShortcutConfig> out;
    std::string error;
    CHECK(parseConfig(text, home, out, error));
    CHECK(error.empty());
    CHECK(out.size() == 1);
    CHECK((out[0].buttons == std::set<int>{BTN_SELECT, BTN_START}));
    CHECK(out[0].holdMs == 1000);
    CHECK(out[0].workingDirectory == "/srv");
    CHECK(out[0].commands.size() == 2);
    CHECK((out[0].commands[0] == std::vector<std::string>{"/usr/bin/foo", "two words", "$(x);y"}));
    CHECK((out[0].commands[1] == std::vector<std::string>{"/usr/local/bin/baz"}));
    return 0;
}

static int test_defaults_and_names() {
    std::vector<ShortcutConfig> out;
    std::string error;
    CHECK(parseConfig(shortcut(R"("buttons":["VIEW"," x "],"commands":[["/bin/true"]])"), home, out, error));
    CHECK(out.size() == 1);
    CHECK((out[0].buttons == std::set<int>{BTN_SELECT, BTN_NORTH}));
    CHECK(out[0].holdMs == 1000);
    CHECK(out[0].workingDirectory == home);
    CHECK(parseConfig(R"({"shortcuts":[]})", home, out, error));
    CHECK(out.empty());
    const std::pair<const char *, int> names[] = {
        {"a", BTN_SOUTH}, {"b", BTN_EAST}, {"x", BTN_NORTH}, {"y", BTN_WEST},
        {"view", BTN_SELECT}, {"menu", BTN_START}, {"guide", BTN_MODE},
        {"lb", BTN_TL}, {"rb", BTN_TR}, {"ls", BTN_THUMBL}, {"rs", BTN_THUMBR}};
    for (const auto &entry : names) CHECK(buttonCode(entry.first) == entry.second);
    CHECK(buttonCode("start") == -1);
    CHECK(buttonCode("") == -1);
    CHECK(parseConfig(shortcut(R"("buttons":["a"],"hold_ms":100,"commands":[["/bin/true"]])"), home, out, error));
    CHECK(out[0].holdMs == 100);
    CHECK(parseConfig(shortcut(R"("buttons":["a"],"hold_ms":60000,"commands":[["/bin/true"]])"), home, out, error));
    CHECK(out[0].holdMs == 60000);
    return 0;
}

static int test_rejections() {
    const std::string ok = R"("buttons":["view"],"commands":[["/bin/true"]])";
    CHECK(rejects("{bad"));
    CHECK(rejects("[]"));
    CHECK(rejects("{}", "shortcuts"));
    CHECK(rejects(R"({"command":["/bin/true"]})", "shortcuts"));
    CHECK(rejects(R"({"shortcuts":{}})"));
    CHECK(rejects(R"({"shortcuts":[1]})"));
    CHECK(rejects(shortcut(R"("buttons":["start"],"commands":[["/bin/true"]])"), "start"));
    CHECK(rejects(shortcut(R"("buttons":["a","A"],"commands":[["/bin/true"]])")));
    CHECK(rejects(shortcut(R"("buttons":[],"commands":[["/bin/true"]])")));
    CHECK(rejects(shortcut(R"("buttons":"a","commands":[["/bin/true"]])")));
    CHECK(rejects(shortcut(R"("buttons":[1],"commands":[["/bin/true"]])")));
    CHECK(rejects(R"({"shortcuts":[{"buttons":["view","menu"],"commands":[["/bin/true"]]},
                                   {"buttons":["menu","view"],"commands":[["/bin/false"]]}]})", "uplicate"));
    for (const char *hold : {"99", "60001", "1.5", "1000.0", "\"1000\"", "-1", "null"})
        CHECK(rejects(shortcut(ok + ",\"hold_ms\":" + hold), "hold_ms"));
    CHECK(rejects(shortcut(R"("buttons":["a"],"commands":[])")));
    CHECK(rejects(shortcut(R"("buttons":["a"])")));
    CHECK(rejects(shortcut(R"("buttons":["a"],"commands":[[]])")));
    CHECK(rejects(shortcut(R"("buttons":["a"],"commands":[[""]])")));
    CHECK(rejects(shortcut(R"("buttons":["a"],"commands":[["/bin/true",2]])")));
    CHECK(rejects(shortcut(R"("buttons":["a"],"commands":["/bin/true"])")));
    CHECK(rejects(shortcut(R"("buttons":["a"],"commands":[["/bin/echo","a\u0000b"]])"), "NUL"));
    CHECK(rejects(shortcut(ok + R"(,"working_directory":"relative/dir")"), "working_directory"));
    CHECK(rejects(shortcut(ok + R"(,"working_directory":"")"), "working_directory"));
    CHECK(rejects(shortcut(ok + R"(,"working_directory":"/a\u0000b")"), "working_directory"));
    CHECK(rejects(shortcut(ok + R"(,"hold":500)"), "hold"));
    CHECK(rejects(R"({"shortcuts":[],"extra":1})", "extra"));
    return 0;
}

static int test_load_file() {
    char dir[] = "/tmp/steamlink-config-test.XXXXXX";
    CHECK(mkdtemp(dir));
    const std::string path = std::string(dir) + "/shortcuts.json";
    std::vector<ShortcutConfig> out(1);
    std::string error;
    CHECK(!loadConfig(path, home, out, error));
    CHECK(out.empty() && !error.empty());
    {
        std::ofstream file(path);
        file << R"({"shortcuts":[{"buttons":["guide"],"commands":[["/bin/true"]]}]})";
    }
    CHECK(loadConfig(path, home, out, error));
    CHECK(out.size() == 1 && out[0].buttons.count(BTN_MODE));
    {
        std::ofstream file(path);
        std::string big = R"({"shortcuts":[]})";
        big.append(65537 - big.size(), ' ');
        file << big;
    }
    CHECK(!loadConfig(path, home, out, error));
    CHECK(error.find("64 KiB") != std::string::npos);
    {
        std::ofstream file(path);
        std::string fits = R"({"shortcuts":[]})";
        fits.append(65536 - fits.size(), ' ');
        file << fits;
    }
    CHECK(loadConfig(path, home, out, error));
    unlink(path.c_str());
    rmdir(dir);
    return 0;
}

static int test_config_path() {
    setenv("STEAMLINK_TOOL_CONFIG", "/custom/file.json", 1);
    CHECK(configPath() == "/custom/file.json");
    unsetenv("STEAMLINK_TOOL_CONFIG");
    setenv("XDG_CONFIG_HOME", "/xdg", 1);
    setenv("HOME", "/home/someone", 1);
    CHECK(configPath() == "/xdg/steamlink-tool/shortcuts.json");
    setenv("XDG_CONFIG_HOME", "relative", 1);
    CHECK(configPath() == "/home/someone/.config/steamlink-tool/shortcuts.json");
    unsetenv("XDG_CONFIG_HOME");
    CHECK(configPath() == "/home/someone/.config/steamlink-tool/shortcuts.json");
    return 0;
}

int main() {
    if (test_example() || test_defaults_and_names() || test_rejections() || test_load_file() || test_config_path())
        return 1;
    std::puts("PASS config schema, defaults, validation, size limit and path");
}
