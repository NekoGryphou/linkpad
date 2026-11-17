#pragma once
// Shortcut configuration: ~/.config/linkpad/shortcuts.json.
// Independent of Qt. nlohmann/json is used only through its non-throwing API.
#include <nlohmann/json.hpp>

#include <linux/input-event-codes.h>
#include <pwd.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

struct ShortcutConfig {
    std::set<int> buttons;
    int holdMs = 1000;
    std::string workingDirectory;
    std::vector<std::vector<std::string>> commands;
};

constexpr int minimumHoldMs = 100;
constexpr int maximumHoldMs = 60000;
constexpr std::size_t maximumConfigBytes = 64 * 1024;

// Xbox button name to evdev key code, case-insensitive; -1 if unknown.
inline int buttonCode(const std::string &name) {
    std::size_t begin = 0, end = name.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(name[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(name[end - 1]))) --end;
    std::string key;
    for (std::size_t i = begin; i < end; ++i)
        key += static_cast<char>(std::tolower(static_cast<unsigned char>(name[i])));
    static const std::pair<const char *, int> names[] = {
        {"a", BTN_SOUTH}, {"b", BTN_EAST}, {"x", BTN_NORTH}, {"y", BTN_WEST},
        {"view", BTN_SELECT}, {"menu", BTN_START}, {"guide", BTN_MODE},
        {"lb", BTN_TL}, {"rb", BTN_TR}, {"ls", BTN_THUMBL}, {"rs", BTN_THUMBR},
    };
    for (const auto &entry : names) {
        if (key == entry.first) return entry.second;
    }
    return -1;
}

inline std::string homeDirectory() {
    const char *home = std::getenv("HOME");
    if (home && *home == '/') return home;
    const passwd *entry = getpwuid(getuid());
    return entry && entry->pw_dir ? entry->pw_dir : "/";
}

inline std::string configPath() {
    const char *custom = std::getenv("LINKPAD_CONFIG");
    if (custom && *custom) return custom;
    const char *xdg = std::getenv("XDG_CONFIG_HOME");
    const std::string base = xdg && *xdg == '/' ? std::string(xdg) : homeDirectory() + "/.config";
    return base + "/linkpad/shortcuts.json";
}

namespace config_detail {

inline bool fail(std::string &error, const std::string &message) {
    error = message;
    return false;
}

inline bool hasNul(const std::string &text) {
    return text.find('\0') != std::string::npos;
}

inline bool parseShortcut(const nlohmann::json &json, std::size_t index, const std::string &home,
                          ShortcutConfig &out, std::string &error) {
    const std::string where = "shortcut " + std::to_string(index + 1) + ": ";
    if (!json.is_object()) return fail(error, where + "must be an object");
    for (const auto &item : json.items()) {
        const std::string &key = item.key();
        if (key != "buttons" && key != "hold_ms" && key != "working_directory" && key != "commands")
            return fail(error, where + "unknown key \"" + key + "\"");
    }

    const auto buttons = json.find("buttons");
    if (buttons == json.end() || !buttons->is_array() || buttons->empty())
        return fail(error, where + "buttons must be a nonempty array of button names");
    for (const auto &button : *buttons) {
        if (!button.is_string()) return fail(error, where + "button names must be strings");
        const std::string &name = button.get_ref<const std::string &>();
        const int code = buttonCode(name);
        if (code < 0) return fail(error, where + "unknown button \"" + name + "\"");
        if (!out.buttons.insert(code).second) return fail(error, where + "repeated button \"" + name + "\"");
    }

    const auto hold = json.find("hold_ms");
    if (hold != json.end()) {
        long long value = -1;
        if (hold->is_number_unsigned()) {
            const auto unsignedValue = hold->get<unsigned long long>();
            value = unsignedValue > static_cast<unsigned long long>(maximumHoldMs) ? maximumHoldMs + 1LL
                                                                                    : static_cast<long long>(unsignedValue);
        } else if (hold->is_number_integer()) {
            value = hold->get<long long>();
        }
        if (value < minimumHoldMs || value > maximumHoldMs)
            return fail(error, where + "hold_ms must be an integer from " + std::to_string(minimumHoldMs) +
                                   " to " + std::to_string(maximumHoldMs));
        out.holdMs = static_cast<int>(value);
    }

    out.workingDirectory = home;
    const auto directory = json.find("working_directory");
    if (directory != json.end()) {
        if (!directory->is_string()) return fail(error, where + "working_directory must be an absolute path");
        const std::string &path = directory->get_ref<const std::string &>();
        if (path.empty() || path[0] != '/' || hasNul(path))
            return fail(error, where + "working_directory must be an absolute path");
        out.workingDirectory = path;
    }

    const auto commands = json.find("commands");
    if (commands == json.end() || !commands->is_array() || commands->empty())
        return fail(error, where + "commands must be a nonempty array of argument arrays");
    for (const auto &command : *commands) {
        if (!command.is_array() || command.empty())
            return fail(error, where + "each command must be a nonempty array of strings");
        std::vector<std::string> argv;
        for (const auto &argument : command) {
            if (!argument.is_string()) return fail(error, where + "command arguments must be strings");
            const std::string &text = argument.get_ref<const std::string &>();
            if (hasNul(text)) return fail(error, where + "command arguments must not contain NUL");
            argv.push_back(text);
        }
        if (argv[0].empty()) return fail(error, where + "command executable is empty");
        out.commands.push_back(std::move(argv));
    }
    return true;
}

} // namespace config_detail

// On failure `out` is empty and `error` describes the first problem.
inline bool parseConfig(const std::string &text, const std::string &home,
                        std::vector<ShortcutConfig> &out, std::string &error) {
    using config_detail::fail;
    out.clear();
    const nlohmann::json json = nlohmann::json::parse(text, nullptr, false);
    if (json.is_discarded()) return fail(error, "configuration is not valid JSON");
    if (!json.is_object()) return fail(error, "configuration must be a JSON object with a shortcuts array");
    for (const auto &item : json.items()) {
        if (item.key() != "shortcuts")
            return fail(error, "unknown key \"" + item.key() + "\"; the file must contain only a shortcuts array");
    }
    const auto shortcuts = json.find("shortcuts");
    if (shortcuts == json.end() || !shortcuts->is_array())
        return fail(error, "configuration must contain a shortcuts array");

    std::vector<ShortcutConfig> parsed;
    for (std::size_t i = 0; i < shortcuts->size(); ++i) {
        ShortcutConfig shortcut;
        if (!config_detail::parseShortcut((*shortcuts)[i], i, home, shortcut, error)) return false;
        for (const auto &other : parsed) {
            if (other.buttons == shortcut.buttons)
                return fail(error, "shortcut " + std::to_string(i + 1) + ": duplicate button combination");
        }
        parsed.push_back(std::move(shortcut));
    }
    out = std::move(parsed);
    error.clear();
    return true;
}

inline bool loadConfig(const std::string &path, const std::string &home,
                       std::vector<ShortcutConfig> &out, std::string &error) {
    out.clear();
    std::ifstream file(path, std::ios::binary);
    if (!file) return config_detail::fail(error, path + ": " + std::strerror(errno));
    std::string text(maximumConfigBytes + 1, '\0');
    file.read(&text[0], static_cast<std::streamsize>(text.size()));
    if (file.bad()) return config_detail::fail(error, path + ": read error");
    text.resize(static_cast<std::size_t>(file.gcount()));
    if (text.size() > maximumConfigBytes) return config_detail::fail(error, "configuration exceeds 64 KiB");
    return parseConfig(text, home, out, error);
}
