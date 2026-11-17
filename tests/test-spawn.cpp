#include "spawn.hpp"

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static std::string directory;   // temporary test directory
static std::string target;      // tests/command-target binary

static std::string slurp(const std::string &path) {
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

static bool reaped(pid_t pid) {
    int status = 0;
    return waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static void restoreTestEnvironment() {
    for (const char *key : {"LD_PRELOAD", "LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM", "PATH", "TMPDIR"})
        setenv((std::string("STEAMLINK_ORIGINAL_") + key).c_str(), "", 1);
    setenv("STEAMLINK_ORIGINAL_PATH", "/original/path", 1);
    setenv("STEAMLINK_ORIGINAL_QT_QPA_PLATFORM", "wayland", 1);
    setenv("LD_PRELOAD", "unwanted-steamlink-hook.so", 1);
    setenv("LD_LIBRARY_PATH", "/unwanted/bundled/libraries", 1);
    setenv("QT_PLUGIN_PATH", "/unwanted/plugins", 1);
    setenv("STEAMLINK_PRIVATE", "secret", 1);
}

static int test_arguments_and_environment() {
    restoreTestEnvironment();
    const std::string output = directory + "/result.txt";
    pid_t pid = 0;
    std::string error;
    CHECK(spawnCommand({target, output, "two words", "$(not a shell);literal"}, directory, pid, error));
    CHECK(pid > 0);
    CHECK(reaped(pid));
    const std::string data = slurp(output);
    CHECK(data.find("ARG=two words\nARG=$(not a shell);literal\n") != std::string::npos);
    CHECK(data.find("LD_PRELOAD=\nLD_LIBRARY_PATH=\nQT_PLUGIN_PATH=\nQT_QPA_PLATFORM=wayland\nPATH=/original/path\nTMPDIR=\n") !=
          std::string::npos);
    CHECK(data.find("STEAMLINK_") == std::string::npos);
    return 0;
}

static int test_child_environment() {
    restoreTestEnvironment();
    unsetenv("STEAMLINK_ORIGINAL_TMPDIR");
    setenv("TMPDIR", "/bundled/tmp", 1);
    unsetenv("STEAMLINK_ORIGINAL_PATH");
    setenv("PATH", "/kept/path", 1);
    const std::vector<std::string> env = childEnvironment(environ);
    bool path = false;
    for (const std::string &entry : env) {
        CHECK(entry.rfind("STEAMLINK_", 0) != 0);
        CHECK(entry.rfind("TMPDIR=", 0) != 0);        // no saved value: removed
        CHECK(entry.rfind("LD_PRELOAD=", 0) != 0);    // saved empty: removed
        path |= entry == "PATH=/kept/path";            // PATH without a saved value is kept
    }
    CHECK(path);
    return 0;
}

static int test_restored_path_lookup() {
    restoreTestEnvironment();
    const std::string bin = directory + "/commands";
    CHECK(mkdir(bin.c_str(), 0700) == 0);
    const std::string bare = "only-on-restored-path";
    {
        std::ifstream in(target, std::ios::binary);
        std::ofstream out(bin + "/" + bare, std::ios::binary);
        out << in.rdbuf();
    }
    CHECK(chmod((bin + "/" + bare).c_str(), 0700) == 0);
    setenv("STEAMLINK_ORIGINAL_PATH", bin.c_str(), 1);
    setenv("PATH", "/wrong/parent/path", 1);
    CHECK(resolveExecutable(bare, bin, directory) == bin + "/" + bare);
    CHECK(resolveExecutable(bare, "/wrong/parent/path", directory).empty());
    CHECK(resolveExecutable(bare, "commands", directory) == bin + "/" + bare);   // relative PATH entry
    CHECK(resolveExecutable("commands/" + bare, "", directory) == bin + "/" + bare);
    CHECK(resolveExecutable("commands", bin + ":" + directory, directory).empty());  // a directory is not executable
    pid_t pid = 0;
    std::string error;
    const std::string output = directory + "/path-result";
    CHECK(spawnCommand({bare, output, "from restored PATH"}, directory, pid, error));
    CHECK(reaped(pid));
    CHECK(slurp(output).find("ARG=from restored PATH\n") != std::string::npos);
    return 0;
}

static int test_rejections() {
    restoreTestEnvironment();
    pid_t pid = 0;
    std::string error;
    CHECK(!spawnCommand({"/definitely/not/an/executable"}, directory, pid, error));
    CHECK(!error.empty());
    error.clear();
    const std::string marker = directory + "/should-not-run";
    CHECK(!spawnCommand({target, marker}, directory + "/missing-directory", pid, error));
    CHECK(!error.empty());
    CHECK(!spawnCommand({}, directory, pid, error));
    usleep(200000);
    CHECK(access(marker.c_str(), F_OK) != 0);
    return 0;
}

static int test_working_directory() {
    restoreTestEnvironment();
    const std::string sub = directory + "/cwd";
    CHECK(mkdir(sub.c_str(), 0700) == 0);
    pid_t pid = 0;
    std::string error;
    CHECK(spawnCommand({target, "relative-output"}, sub, pid, error));
    CHECK(reaped(pid));
    CHECK(access((sub + "/relative-output").c_str(), F_OK) == 0);
    return 0;
}

static int test_partial_failure() {
    restoreTestEnvironment();
    const std::vector<std::vector<std::string>> commands = {
        {target, directory + "/first"},
        {"/missing/command"},
        {target, directory + "/third"},
    };
    const std::vector<SpawnResult> results = spawnCommands(commands, directory);
    CHECK(results.size() == 3);
    CHECK(results[0].pid > 0 && results[0].error.empty());
    CHECK(results[1].pid == 0 && !results[1].error.empty());
    CHECK(results[2].pid > 0 && results[2].error.empty());
    CHECK(reaped(results[0].pid));
    CHECK(reaped(results[2].pid));
    CHECK(access((directory + "/first").c_str(), F_OK) == 0);
    CHECK(access((directory + "/third").c_str(), F_OK) == 0);
    return 0;
}

int main() {
    char self[4096] = {};
    if (readlink("/proc/self/exe", self, sizeof self - 1) <= 0) return 1;
    target = std::string(self);
    target = target.substr(0, target.rfind('/')) + "/command-target";
    char temporary[] = "/tmp/steamlink-spawn-test.XXXXXX";
    if (!mkdtemp(temporary)) return 1;
    directory = temporary;
    const int failed = test_arguments_and_environment() || test_child_environment() || test_restored_path_lookup() ||
                       test_rejections() || test_working_directory() || test_partial_failure();
    if (std::system(("/bin/rm -rf '" + directory + "'").c_str()) != 0) std::fprintf(stderr, "cleanup failed\n");
    if (failed) return 1;
    std::puts("PASS command arguments, clean environment, restored PATH, working directory and partial failure");
}
