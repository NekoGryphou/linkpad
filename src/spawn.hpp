#pragma once
// Start configured commands without a shell, detached from Steam Link's
// session and environment. Independent of Qt; the caller reaps the children.
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

// Variables the launcher saved as STEAMLINK_ORIGINAL_<name> before Steam Link
// changed them for its bundled client.
inline const std::vector<std::string> &restoredVariables() {
    static const std::vector<std::string> names = {
        "LD_PRELOAD", "LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM", "PATH", "TMPDIR"};
    return names;
}

// The parent's environment with launcher originals restored: an empty saved
// value unsets the variable, and a missing saved value unsets it too (except
// PATH, which is kept). Every STEAMLINK_* variable is dropped.
inline std::vector<std::string> childEnvironment(char **parent) {
    std::vector<std::string> inherited, result;
    for (char **entry = parent; entry && *entry; ++entry) inherited.emplace_back(*entry);
    auto find = [&](const std::string &name, std::string &value) {
        const std::string prefix = name + "=";
        for (const std::string &entry : inherited) {
            if (entry.compare(0, prefix.size(), prefix) == 0) {
                value = entry.substr(prefix.size());
                return true;
            }
        }
        return false;
    };
    auto restored = [&](const std::string &name) {
        for (const std::string &variable : restoredVariables()) {
            if (variable == name) return true;
        }
        return false;
    };
    for (const std::string &entry : inherited) {
        const std::string name = entry.substr(0, entry.find('='));
        if (name.compare(0, 10, "STEAMLINK_") == 0 || restored(name)) continue;
        result.push_back(entry);
    }
    for (const std::string &name : restoredVariables()) {
        std::string value;
        if (find("STEAMLINK_ORIGINAL_" + name, value)) {
            if (!value.empty()) result.push_back(name + "=" + value);
        } else if (name == "PATH" && find(name, value)) {
            result.push_back(name + "=" + value);
        }
    }
    return result;
}

inline bool isExecutableFile(const std::string &path) {
    struct stat info = {};
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) && access(path.c_str(), X_OK) == 0;
}

inline std::string absoluteFrom(const std::string &directory, const std::string &path) {
    return !path.empty() && path[0] == '/' ? path : directory + "/" + path;
}

// Resolve like execvp, but with the given PATH and relative to cwd; "" if not found.
inline std::string resolveExecutable(const std::string &name, const std::string &path, const std::string &cwd) {
    if (name.empty()) return {};
    if (name.find('/') != std::string::npos) {
        const std::string candidate = absoluteFrom(cwd, name);
        return isExecutableFile(candidate) ? candidate : std::string();
    }
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = path.find(':', start);
        std::string entry = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const std::string candidate = absoluteFrom(cwd, entry.empty() ? cwd : entry) + "/" + name;
        if (isExecutableFile(candidate)) return candidate;
        if (end == std::string::npos) return {};
        start = end + 1;
    }
}

inline bool spawnCommand(const std::vector<std::string> &argv, const std::string &cwd, pid_t &pid, std::string &error) {
    pid = 0;
    if (argv.empty() || argv[0].empty()) {
        error = "command executable is missing";
        return false;
    }
    struct stat info = {};
    if (stat(cwd.c_str(), &info) != 0 || !S_ISDIR(info.st_mode) || access(cwd.c_str(), X_OK) != 0) {
        error = "working directory " + cwd + " does not exist or cannot be entered";
        return false;
    }
    const std::vector<std::string> environment = childEnvironment(environ);
    std::string path = "/usr/local/bin:/usr/bin:/bin";
    for (const std::string &entry : environment) {
        if (entry.compare(0, 5, "PATH=") == 0) path = entry.substr(5);
    }
    const std::string executable = resolveExecutable(argv[0], path, cwd);
    if (executable.empty()) {
        error = argv[0] + " was not found or is not executable";
        return false;
    }

    std::vector<char *> args, envp;
    for (const std::string &argument : argv) args.push_back(const_cast<char *>(argument.c_str()));
    args.push_back(nullptr);
    for (const std::string &entry : environment) envp.push_back(const_cast<char *>(entry.c_str()));
    envp.push_back(nullptr);

    posix_spawnattr_t attributes;
    posix_spawn_file_actions_t actions;
    posix_spawnattr_init(&attributes);
    posix_spawn_file_actions_init(&actions);
    sigset_t none, all;
    sigemptyset(&none);
    sigfillset(&all);
    posix_spawnattr_setsigmask(&attributes, &none);
    posix_spawnattr_setsigdefault(&attributes, &all);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSID | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addclosefrom_np(&actions, STDERR_FILENO + 1);
    posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());
    const int result = posix_spawn(&pid, executable.c_str(), &actions, &attributes, args.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    if (result != 0) {
        pid = 0;
        error = executable + ": " + std::strerror(result);
        return false;
    }
    error.clear();
    return true;
}

struct SpawnResult {
    pid_t pid = 0;       // 0 when the command could not be started
    std::string error;
};

// Start every command in order without waiting; one failure does not stop the rest.
inline std::vector<SpawnResult> spawnCommands(const std::vector<std::vector<std::string>> &commands, const std::string &cwd) {
    std::vector<SpawnResult> results;
    for (const auto &argv : commands) {
        SpawnResult result;
        spawnCommand(argv, cwd, result.pid, result.error);
        results.push_back(std::move(result));
    }
    return results;
}
