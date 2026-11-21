#include "controlserver.h"

#include "lyrics.h"
#include "paths.h"
#include "player.h"
#include "visualizer.h"
#include "wave.h"

#include <QJsonArray>
#include <QCoreApplication>
#include <QJsonDocument>

namespace {
constexpr int kProtocol = 1;
constexpr int kQueueWindow = 60;

QJsonObject message(const QString &type) { return {{"v", kProtocol}, {"type", type}}; }

QJsonObject trackJson(const QVariantMap &t) {
    return {{"videoId", t.value("videoId").toString()}, {"title", t.value("title").toString()},
            {"artist", t.value("artist").toString()}, {"artists", QJsonArray::fromStringList(t.value("artists").toStringList())},
            {"album", t.value("album").toString()}, {"thumb", t.value("thumb").toString()},
            {"durationMs", t.value("durationMs").toLongLong()}};
}
}

ControlServer::ControlServer(Player *player, Lyrics *lyrics, Visualizer *visualizer, Wave *wave,
                             const QString &socketPath, QObject *parent)
    : QObject(parent), m_player(player), m_lyrics(lyrics), m_visualizer(visualizer), m_wave(wave),
      m_path(socketPath.isEmpty() ? Paths::controlSocket() : socketPath) {
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&m_server, &QLocalServer::newConnection, this, &ControlServer::onNewConnection);

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(50);
    connect(&m_debounce, &QTimer::timeout, this, &ControlServer::broadcastState);
    auto schedule = [this] { m_debounce.start(); };
    connect(player, &Player::stateChanged, this, schedule);
    connect(lyrics, &Lyrics::stateChanged, this, schedule);
    connect(lyrics, &Lyrics::linesChanged, this, schedule);
    connect(wave, &Wave::activeChanged, this, schedule);
    connect(wave, &Wave::loadingChanged, this, schedule);
    connect(wave, &Wave::settingsChanged, this, schedule);
    connect(wave, &Wave::ratingChanged, this, schedule);

    m_positionTimer.setInterval(1000);
    connect(&m_positionTimer, &QTimer::timeout, this, [this] {
        if (!m_player->playing())
            return;
        auto m = message("position");
        m["positionMs"] = m_player->positionMs();
        broadcast(m);
    });
    m_positionTimer.start();
    connect(player, &Player::positionChanged, this, [this] {
        if (m_player->playing())
            return;
        auto m = message("position");
        m["positionMs"] = m_player->positionMs();
        broadcast(m);
    });

    connect(visualizer, &Visualizer::levelsChanged, this, [this] {
        if (levelSubscribers() == 0)
            return;
        auto m = message("levels");
        QJsonArray l;
        for (int v : m_visualizer->levelValues())
            l << v;
        m["l"] = l;
        broadcast(m, true);
    });
}

bool ControlServer::listen() {
    QLocalServer::removeServer(m_path);
    if (!m_server.listen(m_path)) {
        qWarning() << "omawave: cannot listen on" << m_path << m_server.errorString();
        return false;
    }
    return true;
}

int ControlServer::levelSubscribers() const {
    int n = 0;
    for (const auto *c : m_clients)
        n += c->levels;
    return n;
}

QJsonObject ControlServer::stateJson() const {
    auto m = message("state");
    const QVariantMap track = m_player->track();
    m["playing"] = m_player->playing();
    m["buffering"] = m_player->buffering();
    m["track"] = m_player->queueIndex() >= 0 ? QJsonValue(trackJson(track)) : QJsonValue(QJsonValue::Null);
    m["positionMs"] = m_player->positionMs();
    m["durationMs"] = m_player->durationMs();
    m["volume"] = m_player->volume();
    m["canNext"] = m_player->canNext();
    m["canPrevious"] = m_player->canPrevious();
    const int offset = qMax(0, m_player->queueIndex() - 10);
    QJsonArray rows;
    for (int i = offset; i < qMin(m_player->queueCount(), offset + kQueueWindow); ++i)
        rows << trackJson(m_player->queue()->row(i));
    m["queue"] = rows;
    m["queueOffset"] = offset;
    m["queueIndex"] = m_player->queueIndex();
    m["queueCount"] = m_player->queueCount();
    m["lyrics"] = QJsonObject{{"state", m_lyrics->state()},
                              {"lines", QJsonArray::fromVariantList(m_lyrics->lines())}};
    m["error"] = m_player->error();
    m["wave"] = QJsonObject{{"active", m_wave->active()}, {"loading", m_wave->loading()}, {"mood", m_wave->mood()},
                            {"activity", m_wave->activity()}, {"character", m_wave->character()}};
    m["rating"] = m_wave->rating();
    return m;
}

void ControlServer::sendTo(QLocalSocket *socket, const QJsonObject &object) {
    socket->write(QJsonDocument(object).toJson(QJsonDocument::Compact) + "\n");
}

void ControlServer::broadcast(const QJsonObject &object, bool onlyLevelClients) {
    const QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact) + "\n";
    for (auto *c : std::as_const(m_clients))
        if (!onlyLevelClients || c->levels)
            c->socket->write(line);
}

void ControlServer::broadcastState() { broadcast(stateJson()); }

void ControlServer::onNewConnection() {
    while (auto *socket = m_server.nextPendingConnection()) {
        auto *client = new Client{socket, {}, false};
        m_clients << client;
        connect(socket, &QLocalSocket::readyRead, this, [this, client] { onData(client); });
        connect(socket, &QLocalSocket::disconnected, this, [this, client] {
            const bool hadLevels = client->levels;
            m_clients.removeOne(client);
            client->socket->deleteLater();
            delete client;
            if (hadLevels)
                emit levelSubscribersChanged();
        });
        auto hello = message("hello");
        hello["version"] = QCoreApplication::applicationVersion();
        sendTo(socket, hello);
        sendTo(socket, stateJson());
    }
}

void ControlServer::onData(Client *client) {
    client->buffer += client->socket->readAll();
    // A line without a newline must not grow without bound.
    if (client->buffer.size() > (1 << 20)) {
        client->socket->disconnectFromServer();
        return;
    }
    int nl;
    while ((nl = client->buffer.indexOf('\n')) >= 0) {
        const auto doc = QJsonDocument::fromJson(client->buffer.left(nl));
        client->buffer.remove(0, nl + 1);
        if (!doc.isObject() || doc.object().value("v").toInt() != kProtocol) {
            auto e = message("error");
            e["message"] = "bad message";
            sendTo(client->socket, e);
            continue;
        }
        handle(client, doc.object());
        if (!m_clients.contains(client))
            return;
    }
}

void ControlServer::handle(Client *client, const QJsonObject &m) {
    const QString cmd = m.value("cmd").toString();
    if (cmd == "playPause") m_player->togglePause();
    else if (cmd == "next") m_player->next();
    else if (cmd == "previous") m_player->previous();
    else if (cmd == "seek") m_player->seek(m.value("positionMs").toVariant().toLongLong());
    else if (cmd == "jump") m_player->jump(m.value("index").toInt());
    else if (cmd == "remove") m_player->removeAt(m.value("index").toInt());
    else if (cmd == "setVolume") m_player->setVolume(m.value("volume").toInt());
    else if (cmd == "wave") {
        if (m.contains("mood")) m_wave->setMood(m.value("mood").toString());
        if (m.contains("activity")) m_wave->setActivity(m.value("activity").toString());
        if (m.contains("character")) m_wave->setCharacter(m.value("character").toString());
        m_wave->start();
    } else if (cmd == "waveStop") m_wave->stop();
    else if (cmd == "rate") {
        const QString rating = m.value("rating").toString();
        if (rating == "like" || rating == "dislike" || rating == "none") {
            m_wave->rate(rating);
        } else {
            auto e = message("error");
            e["message"] = "bad rating";
            sendTo(client->socket, e);
        }
    }
    else if (cmd == "subscribeLevels") {
        const bool on = m.value("on").toBool();
        if (on != client->levels) {
            client->levels = on;
            emit levelSubscribersChanged();
        }
    } else if (cmd == "show") emit showRequested();
    else if (cmd == "quit") emit quitRequested();
    else if (cmd == "status") sendTo(client->socket, stateJson());
    else {
        auto e = message("error");
        e["message"] = "unknown command: " + cmd.left(40);
        sendTo(client->socket, e);
    }
}
