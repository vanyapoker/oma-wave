#include "wave.h"

#include "catalog.h"
#include "player.h"
#include "settings.h"

namespace {
constexpr int kBatch = 10;
constexpr int kLowWater = 3;
}

static const QStringList mixCharacters = {"favourite", "discover", "popular"};

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
    m_previewTimer.setSingleShot(true);
    m_previewTimer.setInterval(250);
    connect(&m_previewTimer, &QTimer::timeout, this, &Wave::requestPreview);
    connect(player, &Player::queueChanged, this, [this] {
        if (!m_startingTracks.isEmpty() && m_player->queueIndex() >= 0 &&
            m_player->track().value("videoId") == m_startingTracks.first().toMap().value("videoId"))
            m_startingTracks.clear();
        emit previewChanged();
        topUp();
    });
    if (catalog)
        connect(catalog, &Catalog::signedInChanged, this, [this] { invalidatePreview(); });
    connect(player, &Player::queueReplaced, this, [this] {
        m_interrupted = true;
        if (!m_starting) {
            m_pendingStart = false;
            m_startingTracks.clear();
        }
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
    if (changed)
        invalidatePreview();
}

QVariantMap Wave::tuning() const {
    return {{"mood", mood()}, {"activity", activity()}, {"character", character()}};
}

bool Wave::staged() const { return !m_active || tuning() != m_activeTuning; }

QVariantList Wave::previewTracks() const {
    if (staged())
        return m_previewTuning == tuning() ? m_prepared.mid(0, 3) : QVariantList();
    if (!m_startingTracks.isEmpty())
        return m_startingTracks.mid(0, 3);
    QVariantList tracks;
    for (int i = m_player->queueIndex(); i >= 0 && i < m_player->queueCount() && tracks.size() < 3; ++i)
        tracks << m_player->queue()->row(i);
    return tracks;
}

QVariantList Wave::mixCards() const {
    QVariantList cards;
    for (const QString &key : mixCharacters) {
        QVariantMap candidate = tuning();
        candidate.insert("character", key);
        const bool current = m_active && candidate == m_activeTuning;
        QVariantList tracks = m_mixBatches.value(key).toList().mid(0, 3);
        if (current && !m_startingTracks.isEmpty())
            tracks = m_startingTracks.mid(0, 3);
        else if (current) {
            tracks.clear();
            for (int i = m_player->queueIndex(); i >= 0 && i < m_player->queueCount() && tracks.size() < 3; ++i)
                tracks << m_player->queue()->row(i);
        }
        cards << QVariantMap{{"character", key}, {"tracks", tracks},
                             {"loading", m_previewLoading && m_requestCharacter == key},
                             {"error", m_mixErrors.value(key).toString()},
                             {"active", current}};
    }
    return cards;
}

void Wave::playMix(const QString &key) {
    if (!mixCharacters.contains(key))
        return;
    setCharacter(key);
    start();
}

void Wave::retryMix(const QString &key) {
    if (!mixCharacters.contains(key))
        return;
    m_mixErrors.remove(key);
    if (key == character())
        m_previewError.clear();
    emit previewChanged();
    requestPreview();
}

void Wave::invalidatePreview() {
    ++m_previewGeneration;
    m_pendingStart = false;
    m_prepared.clear();
    m_previewTuning.clear();
    m_previewError.clear();
    m_mixBatches.clear();
    m_mixErrors.clear();
    m_previewTimer.stop();
    if (m_previewInterest)
        m_previewTimer.start();
    emit previewChanged();
}

void Wave::preparePreview() {
    m_previewInterest = true;
    m_previewTimer.stop();
    requestPreview();
}

void Wave::prepareMixCards() {
    m_cardsInterest = true;
    preparePreview();
}

void Wave::retryPreview() {
    m_previewInterest = true;
    retryMix(character());
}

void Wave::requestPreview() {
    if (m_previewLoading || !m_previewInterest)
        return;
    QString key;
    if (staged() && !m_mixBatches.contains(character()) && !m_mixErrors.contains(character()))
        key = character();
    if (key.isEmpty() && m_cardsInterest) {
        for (const QString &candidate : mixCharacters) {
            QVariantMap candidateTuning = tuning();
            candidateTuning.insert("character", candidate);
            if ((!m_active || candidateTuning != m_activeTuning) &&
                !m_mixBatches.contains(candidate) && !m_mixErrors.contains(candidate)) {
                key = candidate;
                break;
            }
        }
    }
    if (key.isEmpty())
        return;
    if (!m_catalog) {
        m_pendingStart = false;
        const QString error = tr("Could not load a preview. Try again.");
        for (const QString &candidate : mixCharacters)
            m_mixErrors.insert(candidate, error);
        m_previewError = error;
        emit previewChanged();
        return;
    }
    const int generation = m_previewGeneration;
    QVariantMap selected = tuning();
    selected.insert("character", key);
    QVariantMap request = selected;
    request.insert("exclude", QVariantList());
    request.insert("count", kBatch);
    m_previewLoading = true;
    m_requestCharacter = key;
    emit previewChanged();
    m_catalog->request("waveNext", request, [this, generation, selected, key](const QVariant &result, const QString &code, const QString &message) {
        m_previewLoading = false;
        m_requestCharacter.clear();
        if (generation != m_previewGeneration) {
            emit previewChanged();
            if (m_previewInterest && !m_previewTimer.isActive())
                requestPreview();
            return;
        }
        const QVariantList tracks = result.toMap().value("tracks").toList().mid(0, kBatch);
        if (!code.isEmpty() || tracks.isEmpty()) {
            const QString error = !code.isEmpty() ? (message.isEmpty() ? tr("Could not load a preview. Try again.") : message)
                                                  : tr("No songs found. Try different settings.");
            m_mixErrors.insert(key, error);
            if (key == character()) {
                m_pendingStart = false;
                m_previewError = error;
            }
        } else {
            m_mixBatches.insert(key, tracks);
            if (key == character()) {
                m_prepared = tracks;
                m_previewTuning = selected;
                m_previewError.clear();
            }
        }
        emit previewChanged();
        if (m_pendingStart && key == character() && !m_prepared.isEmpty())
            commitPreview();
        requestPreview();
    });
}

void Wave::commitPreview() {
    if (m_prepared.isEmpty() || m_previewTuning != tuning())
        return;
    m_pendingStart = false;
    ++m_serial;
    m_settle.stop();
    m_fetching = false;
    m_activeTuning = m_previewTuning;
    m_startingTracks = m_prepared;
    m_prepared.clear();
    m_mixBatches.remove(character());
    m_starting = true;
    m_player->playTracks(m_startingTracks, 0, false);
    m_starting = false;
    setActive(true);
    m_player->setAutoplay(false);
    emit previewChanged();
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
    if (!changed || m_settings->waveCharacter() != value)
        return;
    m_pendingStart = false;
    m_prepared = m_mixBatches.value(value).toList();
    m_previewTuning = m_prepared.isEmpty() ? QVariantMap() : tuning();
    m_previewError = m_mixErrors.value(value).toString();
    emit previewChanged();
    if (m_previewInterest)
        requestPreview();
}

void Wave::setActive(bool on) {
    if (on == m_active)
        return;
    m_active = on;
    m_exhaustedAt = -2;
    if (!on && m_previewInterest && m_prepared.isEmpty() && m_previewError.isEmpty())
        m_previewTimer.start();
    emit activeChanged();
    emit previewChanged();
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
    QVariantMap result = m_activeTuning;
    result.insert("exclude", exclude);
    result.insert("count", count);
    return result;
}

void Wave::start() {
    if (!staged()) {
        if (!m_player->playing())
            m_player->togglePause();
        return;
    }
    if (!m_previewError.isEmpty())
        return;
    m_pendingStart = true;
    if (!m_prepared.isEmpty() && m_previewTuning == tuning())
        commitPreview();
    else
        preparePreview();
}

void Wave::stop() {
    ++m_serial;
    m_pendingStart = false;
    setLoading(false);
    if (!m_active)
        return;
    setActive(false);
    m_player->setAutoplay(true);
}

void Wave::refill() {
    if (!m_catalog)
        return;
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
    if (m_active && m_startingTracks.isEmpty() && !m_fetching && m_player->queueIndex() >= 0 &&
        m_player->queueCount() - m_player->queueIndex() - 1 < kLowWater && m_player->queueIndex() != m_exhaustedAt)
        refill();
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
