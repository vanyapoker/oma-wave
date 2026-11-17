#include <csignal>
#include <sys/prctl.h>
#include "player.h"

#include "catalog.h"
#include "paths.h"
#include "settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QFile>
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
      m_volume(settings->volume()) {
    m_retry.setInterval(50);
    connect(&m_retry, &QTimer::timeout, this, &Player::connectSocket);
    connect(&m_socket, &QLocalSocket::connected, this, [this] {
        m_retry.stop();
        m_connected = true;
        const QList<QPair<int, const char *>> observed = {{1, "pause"}, {2, "time-pos"}, {3, "duration"},
            {4, "playlist"}, {5, "volume"}, {6, "paused-for-cache"}, {7, "idle-active"}};
        for (const auto &[id, name] : observed)
            m_socket.write(QJsonDocument(QJsonObject{{"command", QJsonArray{"observe_property", id, name}}})
                               .toJson(QJsonDocument::Compact) + "\n");
        for (const auto &line : std::as_const(m_outbox))
            m_socket.write(line);
        m_outbox.clear();
        applyCookies();
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
                        "--prefetch-playlist=yes", "--demuxer-max-bytes=16MiB", "--demuxer-max-back-bytes=4MiB", "--ytdl-format=bestaudio/best",
                        "--script-opts=ytdl_hook-ytdl_path=/usr/bin/yt-dlp",
                        "--volume=" + QString::number(m_settings->volume())};
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

void Player::send(const QVariantList &command) {
    const QJsonObject object{{"command", QJsonArray::fromVariantList(command)}, {"request_id", ++m_requestId}};
    const QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact) + "\n";
    if (m_connected)
        m_socket.write(line);
    else
        m_outbox.append(line);
}

void Player::applyCookies() {
    if (!m_connected)
        return;
    if (m_catalog && m_catalog->signedIn())
        send({"change-list", "ytdl-raw-options", "set", "cookies=" + Paths::cookiesFile()});
    else
        send({"change-list", "ytdl-raw-options", "clr", ""});
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
    if (event == "property-change") {
        onProperty(o.value("name").toString(), o.value("data").toVariant(), o.value("data"));
    } else if (event == "end-file" && o.value("reason").toString() == "error") {
        setError(tr("Could not play “%1”").arg(track().value("title").toString()));
    } else if (event == "seek") {
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
