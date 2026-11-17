#include "lyrics.h"

#include "catalog.h"
#include "player.h"

#include <algorithm>

Lyrics::Lyrics(Catalog *catalog, Player *player) : QObject(player), m_catalog(catalog), m_player(player) {
    connect(player, &Player::trackChanged, this, &Lyrics::onTrackChanged);
    connect(player, &Player::positionChanged, this, &Lyrics::updateIndex);
}

int Lyrics::indexAt(const QList<qint64> &times, qint64 positionMs) {
    const auto it = std::upper_bound(times.begin(), times.end(), positionMs);
    return int(it - times.begin()) - 1;
}

void Lyrics::setState(const QString &state) {
    if (state == m_state)
        return;
    m_state = state;
    emit stateChanged();
}

void Lyrics::clearError() {
    if (m_state == "error")
        setState("none");
}

void Lyrics::onTrackChanged() {
    const QVariantMap track = m_player->track();
    const QString id = track.value("videoId").toString();
    if (id == m_videoId)
        return;
    m_videoId = id;
    m_lines.clear();
    m_times.clear();
    emit linesChanged();
    if (m_index != -1) {
        m_index = -1;
        emit currentIndexChanged();
    }
    if (id.isEmpty() || !m_catalog) {
        setState("none");
        return;
    }
    setState("loading");
    const QStringList artists = track.value("artists").toStringList();
    m_catalog->lyrics(track.value("title").toString(), artists.value(0), track.value("album").toString(),
                      int(track.value("durationMs").toLongLong() / 1000),
                      [this, id](const QVariant &r, const QString &code, const QString &) {
                          if (id != m_videoId)
                              return;
                          if (!code.isEmpty()) {
                              setState("error");
                              return;
                          }
                          const auto map = r.toMap();
                          m_lines = map.value("lines").toList();
                          m_times.clear();
                          for (const auto &line : std::as_const(m_lines))
                              m_times << line.toMap().value("t").toLongLong();
                          emit linesChanged();
                          setState(map.value("state").toString());
                          updateIndex();
                      });
}

void Lyrics::updateIndex() {
    const int index = m_state == "synced" ? indexAt(m_times, m_player->positionMs()) : -1;
    if (index == m_index)
        return;
    m_index = index;
    emit currentIndexChanged();
}
