#include "paths.h"

#include <QDir>
#include <QFile>
#include <unistd.h>

static QString ensureDir(const QString &path) {
    QDir().mkpath(path);
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    return path;
}

QString Paths::runtimeDir() {
    const QString base = qEnvironmentVariable("XDG_RUNTIME_DIR");
    return ensureDir(base.isEmpty() ? QString("/tmp/omawave-%1").arg(getuid()) : base + "/omawave");
}

QString Paths::controlSocket() { return runtimeDir() + "/control.sock"; }
QString Paths::mpvSocket() { return runtimeDir() + "/mpv.sock"; }
QString Paths::cavaConfig() { return runtimeDir() + "/cava.conf"; }

QString Paths::configDir() {
    const QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
    return ensureDir((base.isEmpty() ? QDir::homePath() + "/.config" : base) + "/omawave");
}

QString Paths::authFile() { return configDir() + "/auth.json"; }
QString Paths::cookiesFile() { return configDir() + "/cookies.txt"; }
