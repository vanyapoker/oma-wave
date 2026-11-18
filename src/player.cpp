#include <csignal>
#include <sys/prctl.h>
#include <sys/resource.h>
#include "player.h"

#include "catalog.h"
#include "paths.h"
#include "settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUuid>
#include <utility>
#include <cmath>

// ---- QueueModel ----

QVariant QueueModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const auto &r = m_rows[index.row()];
    switch (role) {
    case VideoId: return r.value("videoId");
    case Title: return r.value("title");
    case Artist: return r.value("artist");
    case Album: return r.value("album");
    case Thumb: return r.value("thumb");
    case DurationMs: return r.value("durationMs");
    case IsCurrent: return index.row() == m_current;
    }
    return {};
}

QHash<int, QByteArray> QueueModel::roleNames() const {
    return {{VideoId, "videoId"}, {Title, "title"}, {Artist, "artist"}, {Album, "album"},
            {Thumb, "thumb"}, {DurationMs, "durationMs"}, {IsCurrent, "isCurrent"}};
}

void QueueModel::setRows(const QList<QVariantMap> &rows, int current) {
    beginResetModel();
    m_rows = rows;
    m_current = current;
    endResetModel();
}

// ---- Player ----

namespace {
QString prefetchTemplate() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty() || !QDir().mkpath(base))
        return {};
    return base + "/next-XXXXXX";
}

QVariantMap flatten(QVariantMap track) {
    // The queue exposes a single artist string; keep the list for MPRIS and the plugin.
    const QStringList artists = track.value("artists").toStringList();
    if (!track.contains("artist"))
        track["artist"] = artists.join(", ");
    return track;
}
}

QString Player::urlFor(const QVariantMap &track) {
    const QString url = track.value("url").toString();
    if (!url.isEmpty())
        return url;
    return "https://music.youtube.com/watch?v=" + track.value("videoId").toString();
}

Player::Player(Catalog *catalog, Settings *settings, const QString &mpvPath, const QStringList &extraArgs,
               QObject *parent)
    : QObject(parent), m_catalog(catalog), m_settings(settings),
      m_mpvPath(mpvPath.isEmpty() ? QStandardPaths::findExecutable("mpv") : mpvPath), m_extraArgs(extraArgs),
      m_volume(settings->volume()), m_prefetchDir(prefetchTemplate()) {
    m_retry.setInterval(50);
    connect(&m_retry, &QTimer::timeout, this, &Player::connectSocket);
    m_idleTimer.setSingleShot(true);
    m_idleTimer.setInterval(60000);
    connect(&m_idleTimer, &QTimer::timeout, this, &Player::suspendMpv);
    m_prefetchTimeout.setSingleShot(true);
    m_prefetchTimeout.setInterval(120000);
    connect(&m_prefetchTimeout, &QTimer::timeout, this, [this] {
        m_failedPrefetchUrl = m_prefetchUrl;
        cancelPrefetch();
    });
    connect(&m_prefetch, &QProcess::finished, this, &Player::prefetchFinished);
    connect(&m_prefetch, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_failedPrefetchUrl = m_prefetchUrl;
            cancelPrefetch();
        }
    });
    m_prefetch.setStandardOutputFile(QProcess::nullDevice());
    m_prefetch.setStandardErrorFile(QProcess::nullDevice());
    // The hook only redirects a completed download. Keeping the original playlist
    // URL means mpv remains the authority for queue identity and ordering.
    if (m_prefetchDir.isValid()) {
        QFile hook(m_prefetchDir.filePath("prefetch.lua"));
        if (hook.open(QIODevice::WriteOnly)) {
            const QByteArray map = QJsonDocument(QJsonArray{m_prefetchDir.filePath("ready")})
                                       .toJson(QJsonDocument::Compact);
            hook.write("local mapping = " + map.mid(1, map.size() - 2) + "\n"
                "local original\n"
                "mp.add_hook('on_load', 5, function()\n"
                "  original = nil\n"
                "  local f = io.open(mapping, 'r')\n"
                "  if not f then return end\n"
                "  local url, file = f:read('*l'), f:read('*l')\n"
                "  f:close()\n"
                "  if url == mp.get_property('stream-open-filename') and file then\n"
                "    local cached = io.open(file, 'rb')\n"
                "    if cached then cached:close(); original = url; mp.set_property('stream-open-filename', file) end\n"
                "  end\n"
                "end)\n"
                "mp.add_hook('on_load_fail', 5, function()\n"
                "  if original then mp.set_property('stream-open-filename', original) end\n"
                "end)\n");
        }
    }
    connect(this, &Player::playingChanged, this, &Player::updateIdleTimer);
    connect(&m_socket, &QLocalSocket::connected, this, [this] {
        m_retry.stop();
        m_connected = true;
        const QList<QPair<int, const char *>> observed = {{1, "pause"}, {2, "time-pos"}, {3, "duration"},
            {4, "playlist"}, {5, "volume"}, {6, "paused-for-cache"}, {7, "idle-active"}};
        for (const auto &[id, name] : observed)
            m_socket.write(QJsonDocument(QJsonObject{{"command", QJsonArray{"observe_property", id, name}}})
                               .toJson(QJsonDocument::Compact) + "\n");
        applyCookies();
        if (m_restoring) {
            sendNow({"set_property", "pause", true});
            const int current = m_queue.current();
            if (current >= 0) {
                const QVariantMap item = m_queue.row(current);
                const QString url = urlFor(item);
                if (!m_meta.contains(url)) m_meta.insert(url, item);
                sendNow({"loadfile", url, "replace"});
            }
            for (int i = 0; i < m_queue.rowCount(); ++i) {
                if (i == current) continue;
                const QVariantMap item = m_queue.row(i);
                const QString url = urlFor(item);
                if (!m_meta.contains(url)) m_meta.insert(url, item);
                sendNow({"loadfile", url, "append"});
            }
            if (current > 0)
                sendNow({"playlist-move", 0, current + 1});
            if (current < 0)
                finishRestore();
        } else {
            for (const auto &line : std::as_const(m_outbox))
                m_socket.write(line);
            m_outbox.clear();
            updateIdleTimer();
        }
    });
    connect(&m_socket, &QLocalSocket::readyRead, this, &Player::onData);
    connect(&m_mpv, &QProcess::finished, this, &Player::onMpvFinished);
    if (m_catalog)
        connect(m_catalog, &Catalog::signedInChanged, this, &Player::applyCookies);
    connect(this, &Player::playingChanged, this, &Player::stateChanged);
    connect(this, &Player::bufferingChanged, this, &Player::stateChanged);
    connect(this, &Player::trackChanged, this, &Player::stateChanged);
    connect(this, &Player::durationChanged, this, &Player::stateChanged);
    connect(this, &Player::volumeChanged, this, &Player::stateChanged);
    connect(this, &Player::queueChanged, this, &Player::stateChanged);
    connect(this, &Player::errorChanged, this, &Player::stateChanged);
    startMpv();
}

Player::~Player() { shutdown(); }

void Player::shutdown() {
    m_stopping = true;
    m_retry.stop();
    cancelPrefetch();
    clearPrefetchMapping();
    m_idleTimer.stop();
    if (m_mpv.state() != QProcess::NotRunning) {
        m_mpv.terminate();
        if (!m_mpv.waitForFinished(1000)) {
            m_mpv.kill();
            m_mpv.waitForFinished(500);
        }
    }
}

void Player::setError(const QString &message) {
    if (message == m_error)
        return;
    m_error = message;
    emit errorChanged();
}

void Player::clearError() { setError({}); }

void Player::startMpv() {
    if (m_mpvPath.isEmpty()) {
        setError(tr("mpv is not installed"));
        return;
    }
    QFile::remove(Paths::mpvSocket());
    QStringList args = {"--idle=yes", "--no-video", "--no-config", "--no-terminal",
                        "--input-ipc-server=" + Paths::mpvSocket(), "--audio-client-name=omawave",
                        "--cache-on-disk=yes", "--demuxer-max-bytes=4MiB", "--demuxer-max-back-bytes=4MiB",
                        "--ytdl-format=bestaudio/best",
                        "--script-opts=ytdl_hook-ytdl_path=/usr/bin/yt-dlp",
                        "--volume=" + QString::number(m_settings->volume())};
    if (QFile::exists(m_prefetchDir.filePath("prefetch.lua")))
        args << "--script=" + m_prefetchDir.filePath("prefetch.lua");
    args += m_extraArgs;
    m_mpvStarted.start();
    m_connected = false;
    m_connectAttempts = 0;
    m_buffer.clear();
    m_mpv.setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGTERM); });
    m_mpv.start(m_mpvPath, args);
    m_retry.start();
}

void Player::connectSocket() {
    if (m_connected)
        return;
    if (++m_connectAttempts > 60) {
        m_retry.stop();
        setError(tr("Could not start mpv"));
        return;
    }
    m_socket.abort();
    m_socket.connectToServer(Paths::mpvSocket());
}

void Player::onMpvFinished() {
    cancelPrefetch();
    clearPrefetchMapping();
    releaseRetiredPrefetch();
    if (m_suspended && !m_stopping) {
        m_retry.stop();
        m_socket.abort();
        m_connected = false;
        if (!m_outbox.isEmpty()) {
            m_suspended = false;
            m_restoring = true;
            startMpv();
        }
        return;
    }
    if (m_stopping)
        return;
    const bool longLived = m_mpvStarted.hasExpired(60000);
    m_retry.stop();
    m_socket.abort();
    m_connected = false;
    m_outbox.clear();
    m_queue.setRows({}, -1);
    m_paused = true;
    m_idle = true;
    emit queueChanged();
    emit trackChanged();
    emit playingChanged();
    setError(tr("mpv stopped unexpectedly"));
    // One restart per minute keeps a crashing mpv from spinning.
    if (longLived)
        startMpv();
}

void Player::sendNow(const QVariantList &command) {
    const QJsonObject object{{"command", QJsonArray::fromVariantList(command)}, {"request_id", ++m_requestId}};
    m_socket.write(QJsonDocument(object).toJson(QJsonDocument::Compact) + "\n");
}

void Player::send(const QVariantList &command) {
    const QJsonObject object{{"command", QJsonArray::fromVariantList(command)}, {"request_id", ++m_requestId}};
    const QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact) + "\n";
    m_idleTimer.stop();
    if (m_connected && !m_suspended && !m_restoring) {
        m_socket.write(line);
    } else {
        m_outbox.append(line);
        if (m_suspended && m_mpv.state() == QProcess::NotRunning) {
            m_suspended = false;
            m_restoring = true;
            startMpv();
        }
    }
    if (!m_restoring && !m_suspended) updateIdleTimer();
}

void Player::updateIdleTimer() {
    if (!m_stopping && !m_suspended && !m_restoring && m_connected && !playing())
        m_idleTimer.start();
    else
        m_idleTimer.stop();
}

void Player::suspendMpv() {
    if (playing() || !m_connected || m_restoring || m_stopping) return;
    m_suspended = true;
    cancelPrefetch();
    clearPrefetchMapping();
    m_idleTimer.stop();
    m_mpv.terminate();
}

void Player::finishRestore() {
    if (!m_restoring) return;
    if (m_queue.current() >= 0 && m_positionMs > 0)
        sendNow({"seek", m_positionMs / 1000.0, "absolute+exact"});
    m_restoring = false;
    for (const auto &line : std::as_const(m_outbox))
        m_socket.write(line);
    m_outbox.clear();
    // Observations during reconstruction were ignored; request fresh values.
    const QList<QPair<int, const char *>> observed = {{1, "pause"}, {2, "time-pos"}, {3, "duration"},
        {4, "playlist"}, {5, "volume"}, {6, "paused-for-cache"}, {7, "idle-active"}};
    for (const auto &[id, name] : observed) {
        sendNow({"unobserve_property", id});
        m_socket.write(QJsonDocument(QJsonObject{{"command", QJsonArray{"observe_property", id, name}}})
                           .toJson(QJsonDocument::Compact) + "\n");
    }
    updateIdleTimer();
}

void Player::applyCookies() {
    cancelPrefetch();
    clearPrefetchMapping();
    if (!m_connected)
        return;
    if (m_catalog && m_catalog->signedIn())
        sendNow({"change-list", "ytdl-raw-options", "set", "cookies=" + Paths::cookiesFile()});
    else
        sendNow({"change-list", "ytdl-raw-options", "clr", ""});
    m_failedPrefetchUrl.clear();
    updatePrefetch();
}

void Player::clearPrefetchMapping() {
    if (!m_prefetchDir.isValid()) return;
    QFile::remove(m_prefetchDir.filePath("ready"));
    if (!m_readyFile.isEmpty()) {
        // A hook may already have read the old mapping. Keep its file until
        // mpv has finished opening a file (or has stopped).
        m_retiredPrefetch << m_readyFile;
        m_readyFile.clear();
        m_readyUrl.clear();
    }
}

void Player::releaseRetiredPrefetch() {
    for (const QString &file : std::as_const(m_retiredPrefetch))
        QFile::remove(file);
    m_retiredPrefetch.clear();
}

void Player::cancelPrefetch() {
    m_prefetchTimeout.stop();
    const QString file = std::exchange(m_prefetchFile, {});
    m_prefetchUrl.clear();
    if (m_prefetch.state() != QProcess::NotRunning) {
        m_prefetch.kill();
        m_prefetch.waitForFinished(1000);
    }
    if (!file.isEmpty()) QFile::remove(file);
}

void Player::updatePrefetch() {
    // Keep the mapping until mpv's on_load hook has opened the selected track.
    if (!m_readyUrl.isEmpty() && queueIndex() >= 0 && urlFor(track()) == m_readyUrl)
        return;
    QString wanted;
    if (!m_stopping && !m_suspended && !m_restoring && m_connected &&
        m_prefetchDir.isValid() && queueIndex() >= 0 && queueIndex() + 1 < queueCount())
        wanted = urlFor(m_queue.row(queueIndex() + 1));
    // Do not download arbitrary URLs or local files supplied by a queue caller.
    if (!wanted.startsWith("https://music.youtube.com/watch?v=") ||
        wanted.contains('\n') || wanted.contains('\r'))
        wanted.clear();
    if (wanted != m_failedPrefetchUrl) m_failedPrefetchUrl.clear();
    if (wanted != m_prefetchUrl && !m_prefetchUrl.isEmpty()) cancelPrefetch();
    if (wanted != m_readyUrl && !m_readyUrl.isEmpty()) clearPrefetchMapping();
    if (wanted.isEmpty() || wanted == m_readyUrl || wanted == m_prefetchUrl ||
        wanted == m_failedPrefetchUrl || !m_retiredPrefetch.isEmpty() ||
        !QFile::exists(m_prefetchDir.filePath("prefetch.lua")))
        return;

    m_prefetchUrl = wanted;
    m_prefetchFile = m_prefetchDir.filePath(QUuid::createUuid().toString(QUuid::Id128));
    QStringList args = {"--ignore-config", "--no-playlist", "--no-part", "--no-progress",
                        "--max-filesize", "32M", "-f",
                        "bestaudio[protocol=https]/bestaudio[protocol=http]/best[protocol=https]/best[protocol=http]",
                        "-o", m_prefetchFile};
    if (m_catalog && m_catalog->signedIn())
        args << "--cookies" << Paths::cookiesFile();
    args << "--" << wanted;
    m_prefetch.setChildProcessModifier([] {
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        const rlimit limit = {32 * 1024 * 1024, 32 * 1024 * 1024};
        setrlimit(RLIMIT_FSIZE, &limit);
    });
    m_prefetch.start("/usr/bin/yt-dlp", args);
    m_prefetchTimeout.start();
}

void Player::prefetchFinished(int exitCode, QProcess::ExitStatus status) {
    if (m_prefetchUrl.isEmpty()) return;
    m_prefetchTimeout.stop();
    const QString url = std::exchange(m_prefetchUrl, {});
    const QString file = std::exchange(m_prefetchFile, {});
    const QFileInfo info(file);
    if (status != QProcess::NormalExit || exitCode != 0 || !info.isFile() ||
        info.size() <= 0 || info.size() > 32 * 1024 * 1024) {
        QFile::remove(file);
        m_failedPrefetchUrl = url;
        return;
    }
    QSaveFile mapping(m_prefetchDir.filePath("ready"));
    if (!mapping.open(QIODevice::WriteOnly) ||
        mapping.write(url.toUtf8() + '\n' + file.toUtf8() + '\n') < 0 || !mapping.commit()) {
        QFile::remove(file);
        m_failedPrefetchUrl = url;
        return;
    }
    m_readyUrl = url;
    m_readyFile = file;
}

void Player::onData() {
    m_buffer += m_socket.readAll();
    int nl;
    while ((nl = m_buffer.indexOf('\n')) >= 0) {
        const auto doc = QJsonDocument::fromJson(m_buffer.left(nl));
        m_buffer.remove(0, nl + 1);
        if (doc.isObject())
            handleMessage(doc.object());
    }
}

void Player::handleMessage(const QJsonObject &o) {
    const QString event = o.value("event").toString();
    if (event == "file-loaded") {
        if (!m_readyUrl.isEmpty() && queueIndex() >= 0 && urlFor(track()) == m_readyUrl)
            clearPrefetchMapping();
        releaseRetiredPrefetch();
        if (m_restoring) finishRestore();
        updatePrefetch();
    } else if (event == "property-change") {
        if (!m_suspended && !m_restoring)
            onProperty(o.value("name").toString(), o.value("data").toVariant(), o.value("data"));
    } else if (event == "end-file" && o.value("reason").toString() == "error") {
        if (m_restoring) finishRestore();
        setError(tr("Could not play “%1”").arg(track().value("title").toString()));
    } else if (event == "seek") {
        m_positionClock.invalidate();
        emit seeked(m_positionMs);
    }
}

void Player::onProperty(const QString &name, const QVariant &value, const QJsonValue &raw) {
    if (name == "pause" || name == "idle-active") {
        const bool before = playing();
        (name == "pause" ? m_paused : m_idle) = value.toBool();
        if (before != playing())
            emit playingChanged();
        if (name == "idle-active" && m_idle) {
            m_positionMs = 0;
            emit positionChanged();
        }
    } else if (name == "time-pos") {
        const qint64 ms = value.isValid() && !value.isNull() ? qint64(std::llround(value.toDouble() * 1000)) : 0;
        if (!m_positionClock.isValid() || m_positionClock.elapsed() >= 250 || ms == 0 ||
            std::llabs(ms - m_positionMs) > 1500) {
            m_positionClock.start();
            m_positionMs = ms;
            emit positionChanged();
        }
    } else if (name == "duration") {
        const qint64 ms = value.isValid() && !value.isNull() ? qint64(std::llround(value.toDouble() * 1000)) : 0;
        if (ms != m_durationMs) {
            m_durationMs = ms;
            emit durationChanged();
        }
    } else if (name == "volume") {
        const int v = qRound(value.toDouble());
        if (v != m_volume) {
            m_volume = v;
            emit volumeChanged();
        }
    } else if (name == "paused-for-cache") {
        if (value.toBool() != m_buffering) {
            m_buffering = value.toBool();
            emit bufferingChanged();
        }
    } else if (name == "playlist") {
        const QVariantMap before = track();
        const int beforeIndex = m_queue.current();
        const int beforeCount = m_queue.rowCount();
        QList<QVariantMap> rows;
        int current = -1;
        const QJsonArray array = raw.toArray();
        m_urls.clear();
        for (int i = 0; i < array.size(); ++i) {
            const auto item = array[i].toObject();
            const QString url = item.value("filename").toString();
            m_urls << url;
            QVariantMap row = m_meta.value(url);
            if (row.isEmpty())
                row = {{"title", url}, {"url", url}};
            rows << row;
            if (item.value("current").toBool())
                current = i;
        }
        m_queue.setRows(rows, current);
        updatePrefetch();
        emit queueChanged();
        if (m_queue.row(current) != before || current != beforeIndex || (beforeCount == 0) != rows.isEmpty())
            emit trackChanged();
        maybeAutoplay();
    }
}

bool Player::hasUrl(const QString &url) const { return m_urls.contains(url); }

void Player::remember(const QVariantMap &track) {
    const QVariantMap flat = flatten(track);
    m_meta.insert(urlFor(flat), flat);
}

void Player::append(const QVariantMap &track) {
    remember(track);
    send({"loadfile", urlFor(track), "append"});
}

void Player::appendUnique(const QVariantList &tracks) {
    QSet<QString> seen(m_urls.begin(), m_urls.end());
    for (const auto &t : tracks) {
        const QVariantMap track = t.toMap();
        const QString url = urlFor(track);
        if (seen.contains(url))
            continue;
        seen.insert(url);
        append(track);
    }
}

void Player::playTracks(const QVariantList &tracks, int start, bool radio) {
    if (tracks.isEmpty())
        return;
    emit queueReplaced();
    start = qBound(0, start, int(tracks.size()) - 1);
    const int serial = ++m_playSerial;
    m_meta.clear();
    m_autoplayFor.clear();
    send({"stop"});
    send({"playlist-clear"});
    for (const auto &t : tracks)
        append(t.toMap());
    send({"playlist-play-index", start});
    send({"set_property", "pause", false});
    const QString seed = tracks[start].toMap().value("videoId").toString();
    if (radio && m_catalog && !seed.isEmpty())
        m_catalog->upNext(seed, [this, serial](const QVariant &r, const QString &code, const QString &) {
            if (serial != m_playSerial || !code.isEmpty())
                return;
            appendUnique(r.toMap().value("tracks").toList());
        });
}

void Player::enqueue(const QVariantMap &track) { append(track); }

void Player::appendTracks(const QVariantList &tracks) { appendUnique(tracks); }

void Player::playCollection(const QString &kind, const QString &id) {
    if (!m_catalog)
        return;
    m_catalog->request(kind, {{"id", id}}, [this](const QVariant &result, const QString &code, const QString &message) {
        if (!code.isEmpty()) {
            m_catalog->reportError(message);
            return;
        }
        const QVariantList tracks = result.toMap().value("tracks").toList();
        if (!tracks.isEmpty())
            playTracks(tracks, 0, false);
    });
}

void Player::playNext(const QVariantMap &track) {
    remember(track);
    send({"loadfile", urlFor(track), "insert-next"});
}

void Player::removeAt(int i) { send({"playlist-remove", i}); }
void Player::moveTrack(int from, int to) { send({"playlist-move", from, to > from ? to + 1 : to}); }
void Player::jump(int i) { send({"playlist-play-index", i}); }

void Player::togglePause() {
    if (m_idle && queueCount() > 0) {
        send({"playlist-play-index", qMax(0, queueIndex())});
        send({"set_property", "pause", false});
    } else {
        send({"cycle", "pause"});
    }
}

void Player::next() { send({"playlist-next"}); }

void Player::previous() {
    if (m_positionMs > 3000 || queueIndex() <= 0)
        send({"seek", 0, "absolute"});
    else
        send({"playlist-prev"});
}

void Player::seek(qint64 ms) { send({"seek", ms / 1000.0, "absolute"}); }

void Player::setVolume(int volume) {
    volume = qBound(0, volume, 100);
    send({"set_property", "volume", volume});
    m_settings->setVolume(volume);
}

// When the last queue entry plays, ask YouTube Music for what comes next, once per video.
void Player::maybeAutoplay() {
    if (!m_autoplay || !m_catalog || queueCount() == 0 || queueIndex() != queueCount() - 1 || !playing())
        return;
    const QString id = track().value("videoId").toString();
    if (id.isEmpty() || id == m_autoplayFor)
        return;
    m_autoplayFor = id;
    const int serial = m_playSerial;
    m_catalog->upNext(id, [this, serial](const QVariant &r, const QString &code, const QString &) {
        if (serial != m_playSerial || !code.isEmpty())
            return;
        appendUnique(r.toMap().value("tracks").toList());
    });
}
