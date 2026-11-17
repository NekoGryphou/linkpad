#pragma once
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <unistd.h>

struct Action {
    QStringList command;
    QString workingDirectory;
};

inline QString actionConfigPath() {
    const QString overridePath = qEnvironmentVariable("STEAMLINK_TOOL_CONFIG");
    if (!overridePath.isEmpty()) return overridePath;
    QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (base.isEmpty()) base = QDir::homePath() + "/.config";
    return base + "/steamlink-tool/action.json";
}

inline bool readAction(const QString &path, Action &action, QString &error) {
    action = Action();
    auto reject = [&](const QString &message) { error = message; return false; };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return reject(file.errorString());
    const QByteArray bytes = file.read(16385);
    if (bytes.size() > 16384) return reject("Configuration exceeds 16 KiB");
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) return reject("Expected a JSON object");
    const QJsonObject json = doc.object();
    Action parsed;
        if (!json.value("command").isArray()) return reject("command must be an array of strings");
        for (const QJsonValue &value : json.value("command").toArray()) {
            if (!value.isString() || value.toString().contains(QChar(0))) return reject("Command arguments must be strings without NUL");
            parsed.command.append(value.toString());
        }
        if (parsed.command.isEmpty() || parsed.command.first().isEmpty()) return reject("Command executable is missing");
        parsed.workingDirectory = QDir::homePath();
        if (json.contains("working_directory")) {
            if (!json.value("working_directory").isString()) return reject("working_directory must be a string");
            parsed.workingDirectory = json.value("working_directory").toString();
            if (!QDir::isAbsolutePath(parsed.workingDirectory) || parsed.workingDirectory.contains(QChar(0))) return reject("working_directory must be an absolute path");
        }
    action = parsed; error.clear(); return true;
}

inline bool launchActionCommand(const Action &action, qint64 *pid, QString &error) {
    if (action.command.isEmpty()) {
        error = "No command configured"; return false;
    }
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // Valve changes these variables for its bundled client. External tools get
    // the launcher's original environment, without the extension injected.
    for (const char *name : {"LD_PRELOAD", "LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM", "PATH", "TMPDIR"}) {
        const QString key = QString::fromLatin1(name);
        const QString saved = "STEAMLINK_ORIGINAL_" + key;
        if (env.contains(saved)) {
            const QString value = env.value(saved);
            if (value.isEmpty()) env.remove(key); else env.insert(key, value);
        } else if (key != "PATH") env.remove(key);
    }
    for (const QString &key : env.keys()) if (key.startsWith("STEAMLINK_")) env.remove(key);
    if (!QFileInfo(action.workingDirectory).isDir() ||
        ::access(action.workingDirectory.toLocal8Bit().constData(), X_OK) != 0) {
        error = "Working directory does not exist or cannot be entered";
        return false;
    }
    // Qt 5.14's detached launch resolves bare names with the parent's PATH,
    // and continues after a failed chdir. Validate both before giving Qt an
    // absolute executable path and the already checked working directory.
    QString executable;
    auto usable = [](const QString &path) {
        return QFileInfo(path).isFile() && ::access(path.toLocal8Bit().constData(), X_OK) == 0;
    };
    const QString requested = action.command.first();
    if (requested.contains('/')) {
        const QString candidate = QDir::isAbsolutePath(requested) ? requested :
            QDir(action.workingDirectory).absoluteFilePath(requested);
        if (usable(candidate)) executable = candidate;
    } else {
        const QString path = env.value("PATH", "/usr/local/bin:/usr/bin:/bin");
        for (int i = 0; i <= path.count(':'); ++i) {
            QString directory = path.section(':', i, i);
            if (directory.isEmpty()) directory = action.workingDirectory;
            else if (!QDir::isAbsolutePath(directory)) directory = QDir(action.workingDirectory).absoluteFilePath(directory);
            const QString candidate = QDir(directory).absoluteFilePath(requested);
            if (usable(candidate)) { executable = candidate; break; }
        }
    }
    if (executable.isEmpty()) {
        error = "Command executable was not found or is not executable";
        return false;
    }
    QProcess process;
    process.setProcessEnvironment(env);
    process.setProgram(executable);
    process.setArguments(action.command.mid(1));
    process.setWorkingDirectory(action.workingDirectory);
    const bool started = process.startDetached(pid);
    error = started ? QString() : process.errorString();
    return started;
}
