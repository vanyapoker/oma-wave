#include "wave.h"

#include "catalog.h"
#include "player.h"
#include "settings.h"

namespace {
constexpr int kBatch = 10;
constexpr int kLowWater = 3;
}

Wave::Wave(Catalog *catalog, Player *player, Settings *settings, QObject *parent)
    : QObject(parent), m_catalog(catalog), m_player(player), m_settings(settings) {
    // A reply is appended asynchronously; hold off the next top-up until the queue shows it.
    m_settle.setSingleShot(true);
    m_settle.setInterval(400);
    connect(&m_settle, &QTimer::timeout, this, [this] {
        m_fetching = false;
        topUp();
    });
    connect(settings, &Settings::waveChanged, this, &Wave::settingsChanged);
    connect(player, &Player::queueChanged, this, &Wave::topUp);
    connect(player, &Player::queueReplaced, this, [this] {
        m_interrupted = true;
        if (m_active && !m_starting) {
            setActive(false);
            m_player->setAutoplay(true);
        }
    });
    connect(player, &Player::positionChanged, this, [this] { m_logMaxMs = qMax(m_logMaxMs, m_player->positionMs()); });
    connect(player, &Player::durationChanged, this, [this] {
        if (m_player->durationMs() > 0)
            m_logDurationMs = m_player->durationMs();
    });
    connect(player, &Player::trackChanged, this, &Wave::onTrackChanged);
}

QString Wave::mood() const { return m_settings->waveMood(); }
QString Wave::activity() const { return m_settings->waveActivity(); }
QString Wave::character() const { return m_settings->waveCharacter(); }

void Wave::afterSettingChange(bool changed) {
    if (changed && m_active)
        refill(true);
}

void Wave::setMood(const QString &value) {
    const bool changed = value != m_settings->waveMood();
    m_settings->setWaveMood(value);
    afterSettingChange(changed && m_settings->waveMood() == value);
}

void Wave::setActivity(const QString &value) {
    const bool changed = value != m_settings->waveActivity();
    m_settings->setWaveActivity(value);
    afterSettingChange(changed && m_settings->waveActivity() == value);
}

void Wave::setCharacter(const QString &value) {
    const bool changed = value != m_settings->waveCharacter();
    m_settings->setWaveCharacter(value);
    afterSettingChange(changed && m_settings->waveCharacter() == value);
}

void Wave::setActive(bool on) {
    if (on == m_active)
        return;
    m_active = on;
    m_exhaustedAt = -2;
    emit activeChanged();
}

void Wave::setLoading(bool on) {
    if (on == m_loading)
        return;
    m_loading = on;
    emit loadingChanged();
}

void Wave::setRating(const QString &rating) {
    if (rating == m_rating)
        return;
    m_rating = rating;
    emit ratingChanged();
}

QVariantMap Wave::params(const QVariantList &exclude, int count) const {
    return {{"mood", mood()}, {"activity", activity()}, {"character", character()},
            {"exclude", exclude}, {"count", count}};
}

void Wave::start() {
    if (!m_catalog)
        return;
    if (m_active) {
        refill(true);
        return;
    }
    const int serial = ++m_serial;
    setLoading(true);
    m_catalog->request("waveNext", params({}, kBatch),
                       [this, serial](const QVariant &result, const QString &code, const QString &message) {
        if (serial != m_serial)
            return;
        setLoading(false);
        if (!code.isEmpty()) {
            m_catalog->reportError(message);
            return;
        }
        const QVariantList tracks = result.toMap().value("tracks").toList();
        if (tracks.isEmpty()) {
            m_catalog->reportError(tr("My Wave found nothing to play. Try another mood."));
            return;
        }
        m_starting = true;
        m_player->playTracks(tracks, 0, false);
        m_starting = false;
        m_fetching = false;
        setActive(true);
        m_player->setAutoplay(false);
    });
}

void Wave::stop() {
    ++m_serial;
    setLoading(false);
    if (!m_active)
        return;
    setActive(false);
    m_player->setAutoplay(true);
}

void Wave::refill(bool replaceUpcoming) {
    if (!m_catalog)
        return;
    if (replaceUpcoming) {
        ++m_serial;
        m_fetching = false;
        m_settle.stop();
        for (int i = m_player->queueCount() - 1; i > m_player->queueIndex(); --i)
            m_player->removeAt(i);
    }
    if (m_fetching)
        return;
    m_fetching = true;
    QVariantList exclude;
    for (int i = 0; i < m_player->queueCount(); ++i)
        exclude << m_player->queue()->row(i).value("videoId");
    const int serial = m_serial;
    m_catalog->request("waveNext", params(exclude, kBatch),
                       [this, serial](const QVariant &result, const QString &code, const QString &message) {
        if (serial != m_serial || !m_active)
            return;
        if (!code.isEmpty()) {
            m_fetching = false;
            m_exhaustedAt = m_player->queueIndex();
            m_catalog->reportError(message);
            return;
        }
        const QVariantList tracks = result.toMap().value("tracks").toList();
        if (tracks.isEmpty()) {
            m_fetching = false;
            m_exhaustedAt = m_player->queueIndex();
            return;
        }
        m_player->appendTracks(tracks);
        m_settle.start();
    });
}

void Wave::topUp() {
    if (m_active && !m_fetching && m_player->queueIndex() >= 0 &&
        m_player->queueCount() - m_player->queueIndex() - 1 < kLowWater && m_player->queueIndex() != m_exhaustedAt)
        refill(false);
}

void Wave::flushLog() {
    const QString id = m_logTrack.value("videoId").toString();
    if (id.isEmpty() || !m_catalog)
        return;
    const qint64 duration = m_logDurationMs > 0 ? m_logDurationMs : m_logTrack.value("durationMs").toLongLong();
    m_catalog->request("logPlay",
                       {{"track", m_logTrack}, {"listenedMs", m_logMaxMs}, {"durationMs", duration},
                        {"source", m_logSource}, {"interrupted", m_interrupted}},
                       [](const QVariant &, const QString &, const QString &) {});
}

void Wave::onTrackChanged() {
    const QVariantMap now = m_player->queueIndex() >= 0 ? m_player->track() : QVariantMap();
    const QString id = now.value("videoId").toString();
    if (id == m_logTrack.value("videoId").toString())
        return;
    flushLog();
    m_logTrack = now;
    m_logMaxMs = 0;
    m_logDurationMs = 0;
    m_logSource = m_active ? "wave" : "other";
    m_interrupted = false;
    setRating("none");
    if (id.isEmpty() || !m_catalog)
        return;
    m_catalog->request("rating", {{"videoId", id}}, [this, id](const QVariant &result, const QString &code, const QString &) {
        if (code.isEmpty() && m_logTrack.value("videoId").toString() == id)
            setRating(result.toMap().value("rating").toString());
    });
}

void Wave::rate(const QString &rating) {
    if (rating != "like" && rating != "dislike" && rating != "none")
        return;
    if (m_player->queueIndex() < 0 || !m_catalog)
        return;
    setRating(rating);
    m_catalog->request("rate", {{"track", m_player->track()}, {"rating", rating}},
                       [this](const QVariant &, const QString &code, const QString &message) {
        if (!code.isEmpty())
            m_catalog->reportError(message);
    });
}

void Wave::toggleLike() { rate(m_rating == "like" ? "none" : "like"); }

void Wave::dislike() {
    rate("dislike");
    m_player->next();
}
