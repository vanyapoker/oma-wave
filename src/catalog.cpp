#include "catalog.h"

#include "paths.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

namespace {
QString defaultWorker() {
    const QString env = qEnvironmentVariable("OMAWAVE_CATALOG");
    if (!env.isEmpty())
        return env;
    const QString dev = QCoreApplication::applicationDirPath() + "/../catalog/catalog.py";
    if (QFileInfo::exists(dev))
        return QFileInfo(dev).absoluteFilePath();
    return "/usr/lib/omawave/catalog.py";
}
}

Catalog::Catalog(const QString &workerPath, const QString &configDir, QObject *parent)
    : QObject(parent),
      m_workerPath(workerPath.isEmpty() ? defaultWorker() : workerPath),
      m_configDir(configDir.isEmpty() ? Paths::configDir() : configDir) {
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &Catalog::onReadyRead);
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        m_stderr += m_process.readAllStandardError();
        if (m_stderr.size() > 4000)
            m_stderr = m_stderr.right(4000);
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) { onFinished(code); });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            onFinished(127);
    });
    startWorker();
}

Catalog::~Catalog() { shutdown(); }

void Catalog::shutdown() {
    m_stopping = true;
    if (m_process.state() != QProcess::NotRunning) {
        m_process.closeWriteChannel();
        if (!m_process.waitForFinished(500)) {
            m_process.kill();
            m_process.waitForFinished(500);
        }
    }
}

void Catalog::startWorker() {
    m_buffer.clear();
    m_stderr.clear();
    m_started.start();
    m_process.start("/usr/bin/python3", {m_workerPath, "--config-dir", m_configDir});
}

void Catalog::setError(const QString &message) {
    if (message == m_error)
        return;
    m_error = message;
    emit errorChanged();
}

void Catalog::clearError() { setError({}); }

void Catalog::setSignedIn(bool value) {
    if (value == m_signedIn)
        return;
    m_signedIn = value;
    emit signedInChanged();
}

int Catalog::request(const QString &method, const QVariantMap &params, Done done) {
    const int id = m_nextId++;
    if (m_process.state() == QProcess::NotRunning && !m_fatal && !m_stopping && m_started.hasExpired(10000))
        startWorker();
    if (m_process.state() == QProcess::NotRunning) {
        const QString message = tr("Catalogue helper stopped: %1").arg(QString::fromUtf8(m_stderr.right(300)).trimmed());
        QTimer::singleShot(0, this, [done, message] { done({}, "worker_stopped", message); });
        return id;
    }
    const bool wasBusy = busy();
    m_pending.insert(id, std::move(done));
    if (!wasBusy)
        emit busyChanged();
    const QJsonObject object{{"id", id}, {"method", method}, {"params", QJsonObject::fromVariantMap(params)}};
    m_process.write(QJsonDocument(object).toJson(QJsonDocument::Compact) + "\n");
    return id;
}

void Catalog::finish(int id, const QVariant &result, const QString &code, const QString &message) {
    auto it = m_pending.find(id);
    if (it == m_pending.end())
        return;
    Done done = std::move(it.value());
    m_pending.erase(it);
    if (m_pending.isEmpty())
        emit busyChanged();
    if (done)
        done(result, code, message);
}

void Catalog::onReadyRead() {
    m_buffer += m_process.readAllStandardOutput();
    int nl;
    while ((nl = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(nl);
        m_buffer.remove(0, nl + 1);
        handleLine(line);
    }
}

void Catalog::handleLine(const QByteArray &line) {
    const auto doc = QJsonDocument::fromJson(line);
    if (!doc.isObject())
        return;
    const auto object = doc.object();
    const int id = object.value("id").toInt(-1);
    if (id == 0) {
        setSignedIn(object.value("result").toObject().value("signedIn").toBool());
        return;
    }
    if (object.contains("error")) {
        const auto err = object.value("error").toObject();
        finish(id, {}, err.value("code").toString(), err.value("message").toString());
    } else {
        finish(id, object.value("result").toVariant(), {}, {});
    }
}

void Catalog::onFinished(int code) {
    if (m_stopping)
        return;
    const QString tail = QString::fromUtf8(m_stderr.right(300)).trimmed();
    const auto pending = m_pending.keys();
    for (int id : pending)
        finish(id, {}, "worker_stopped", tail);
    if (code == 3) {
        m_fatal = true;
        setError(tail.section('\n', -1));
        return;
    }
    if (m_started.hasExpired(10000))
        startWorker();
    else
        setError(tr("Catalogue helper stopped: %1").arg(tail));
}

void Catalog::loadHome() {
    request("home", {}, [this](const QVariant &r, const QString &code, const QString &msg) {
        if (!code.isEmpty())
            return setError(msg);
        m_home = r.toMap().value("sections").toList();
        emit homeChanged();
    });
}

void Catalog::search(const QString &query) {
    const int serial = ++m_searchSerial;
    if (query.trimmed().isEmpty()) {
        m_search.clear();
        emit searchResultsChanged();
        return;
    }
    request("search", {{"query", query}}, [this, serial](const QVariant &r, const QString &code, const QString &msg) {
        if (serial != m_searchSerial)
            return;
        if (!code.isEmpty())
            return setError(msg);
        m_search = r.toMap();
        emit searchResultsChanged();
    });
}

void Catalog::openDetail(const QString &kind, const QString &id) {
    const int serial = ++m_detailSerial;
    m_detail = {{"kind", kind}, {"id", id}, {"loading", true}, {"tracks", QVariantList()}};
    emit detailChanged();
    request(kind, {{"id", id}}, [this, serial](const QVariant &r, const QString &code, const QString &msg) {
        if (serial != m_detailSerial)
            return;
        if (!code.isEmpty()) {
            m_detail["loading"] = false;
            emit detailChanged();
            return setError(msg);
        }
        m_detail = r.toMap();
        emit detailChanged();
    });
}

void Catalog::loadLibrary() {
    struct Entry { const char *method, *key; };
    static const Entry entries[] = {{"libraryPlaylists", "playlists"}, {"libraryAlbums", "albums"},
                                    {"libraryArtists", "artists"}, {"likedSongs", "liked"}};
    for (const auto &entry : entries) {
        const QString key = entry.key;
        request(entry.method, {}, [this, key](const QVariant &r, const QString &code, const QString &msg) {
            if (code == "not_signed_in")
                return;
            if (!code.isEmpty())
                return setError(msg);
            const auto map = r.toMap();
            m_library[key] = key == "liked" ? map.value("tracks") : map.value("items");
            emit libraryChanged();
        });
    }
}

void Catalog::signIn(const QString &browser) {
    request("signIn", {{"browser", browser}}, [this](const QVariant &r, const QString &code, const QString &msg) {
        if (!code.isEmpty())
            return setError(msg);
        setSignedIn(r.toMap().value("signedIn").toBool());
    });
}

void Catalog::signOut() {
    request("signOut", {}, [this](const QVariant &r, const QString &code, const QString &msg) {
        if (!code.isEmpty())
            return setError(msg);
        setSignedIn(r.toMap().value("signedIn").toBool());
        m_library.clear();
        emit libraryChanged();
    });
}

void Catalog::upNext(const QString &videoId, Done done) { request("upNext", {{"videoId", videoId}}, std::move(done)); }

void Catalog::lyrics(const QString &title, const QString &artist, const QString &album, int durationSec, Done done) {
    request("lyrics", {{"title", title}, {"artist", artist}, {"album", album}, {"durationSec", durationSec}},
            std::move(done));
}
