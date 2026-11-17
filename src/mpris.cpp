#include "mpris.h"

#include "player.h"

#include <QDBusConnection>
#include <QDebug>
#include <QDBusError>
#include <QDBusMessage>

static const char *kPath = "/org/mpris/MediaPlayer2";

Mpris::Mpris(Player *player, QObject *parent) : QObject(parent), m_player(player) {
    new MprisRootAdaptor(this);
    auto *playerAdaptor = new MprisPlayerAdaptor(this);
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.registerObject(kPath, this, QDBusConnection::ExportAdaptors) ||
        !bus.registerService("org.mpris.MediaPlayer2.omawave")) {
        qWarning() << "omawave: MPRIS unavailable:" << bus.lastError().message();
        return;
    }
    connect(player, &Player::stateChanged, this, &Mpris::publish);
    connect(player, &Player::seeked, this, [playerAdaptor](qint64 ms) { emit playerAdaptor->Seeked(ms * 1000); });
    m_last = {{"PlaybackStatus", playbackStatus()}, {"Metadata", metadata()}};
}

QString Mpris::playbackStatus() const {
    if (m_player->queueCount() == 0 || m_player->idle())
        return "Stopped";
    return m_player->playing() ? "Playing" : "Paused";
}

QString Mpris::currentTrackId() const {
    const auto track = m_player->track();
    QString key = track.value("videoId").toString();
    if (key.isEmpty())
        key = track.value("url").toString();
    return "/org/omawave/track/" + (key.isEmpty() ? QString("none") : QString::fromLatin1(key.toUtf8().toHex()));
}

QVariantMap Mpris::metadata() const {
    if (m_player->queueIndex() < 0)
        return {{"mpris:trackid", QVariant::fromValue(QDBusObjectPath("/org/mpris/MediaPlayer2/TrackList/NoTrack"))}};
    const auto t = m_player->track();
    QStringList artists = t.value("artists").toStringList();
    if (artists.isEmpty() && !t.value("artist").toString().isEmpty())
        artists << t.value("artist").toString();
    QVariantMap m{{"mpris:trackid", QVariant::fromValue(QDBusObjectPath(currentTrackId()))},
                  {"xesam:title", t.value("title").toString()},
                  {"xesam:artist", artists},
                  {"xesam:album", t.value("album").toString()},
                  {"mpris:artUrl", t.value("thumb").toString()}};
    const qint64 ms = m_player->durationMs() > 0 ? m_player->durationMs() : t.value("durationMs").toLongLong();
    m.insert("mpris:length", QVariant::fromValue<qlonglong>(ms * 1000));
    return m;
}

void Mpris::publish() {
    const QVariantMap now{{"PlaybackStatus", playbackStatus()}, {"Metadata", metadata()},
                          {"Volume", m_player->volume() / 100.0}, {"CanGoNext", m_player->canNext()},
                          {"CanGoPrevious", m_player->canPrevious()}};
    QVariantMap changed;
    for (auto it = now.begin(); it != now.end(); ++it)
        if (m_last.value(it.key()) != it.value())
            changed.insert(it.key(), it.value());
    if (changed.isEmpty())
        return;
    m_last.insert(changed);
    auto signal = QDBusMessage::createSignal(kPath, "org.freedesktop.DBus.Properties", "PropertiesChanged");
    signal << "org.mpris.MediaPlayer2.Player" << changed << QStringList();
    QDBusConnection::sessionBus().send(signal);
}

MprisPlayerAdaptor::MprisPlayerAdaptor(Mpris *mpris) : QDBusAbstractAdaptor(mpris), m_mpris(mpris) {}

double MprisPlayerAdaptor::volume() const { return m_mpris->player()->volume() / 100.0; }
void MprisPlayerAdaptor::setVolume(double v) { m_mpris->player()->setVolume(qRound(qBound(0.0, v, 1.0) * 100)); }
qlonglong MprisPlayerAdaptor::position() const { return m_mpris->player()->positionMs() * 1000; }
bool MprisPlayerAdaptor::canGoNext() const { return m_mpris->player()->canNext(); }
bool MprisPlayerAdaptor::canGoPrevious() const { return m_mpris->player()->canPrevious(); }

void MprisPlayerAdaptor::Next() { m_mpris->player()->next(); }
void MprisPlayerAdaptor::Previous() { m_mpris->player()->previous(); }
void MprisPlayerAdaptor::PlayPause() { m_mpris->player()->togglePause(); }
void MprisPlayerAdaptor::Pause() {
    if (m_mpris->player()->playing())
        m_mpris->player()->togglePause();
}
void MprisPlayerAdaptor::Stop() { Pause(); }
void MprisPlayerAdaptor::Play() {
    if (!m_mpris->player()->playing())
        m_mpris->player()->togglePause();
}
void MprisPlayerAdaptor::Seek(qlonglong offsetUs) {
    auto *p = m_mpris->player();
    p->seek(qMax<qint64>(0, p->positionMs() + offsetUs / 1000));
}
void MprisPlayerAdaptor::SetPosition(const QDBusObjectPath &trackId, qlonglong positionUs) {
    if (trackId.path() != m_mpris->currentTrackId())
        return;
    m_mpris->player()->seek(positionUs / 1000);
}
void MprisPlayerAdaptor::OpenUri(const QString &) {
    m_mpris->replyNotSupported("OpenUri is not supported");
}

void Mpris::replyNotSupported(const QString &what) {
    if (calledFromDBus())
        sendErrorReply(QDBusError::NotSupported, what);
}
