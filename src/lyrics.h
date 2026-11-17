#pragma once

#include <QObject>
#include <QVariantList>

class Catalog;
class Player;

// Lyrics for the current track, fetched through the catalogue worker (LRCLIB).
class Lyrics : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QVariantList lines READ lines NOTIFY linesChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)

public:
    Lyrics(Catalog *catalog, Player *player);

    QString state() const { return m_state; }
    QVariantList lines() const { return m_lines; }
    int currentIndex() const { return m_index; }

    // Index of the last line whose time is <= positionMs, or -1 before the first.
    static int indexAt(const QList<qint64> &times, qint64 positionMs);

    Q_INVOKABLE void clearError();

signals:
    void stateChanged();
    void linesChanged();
    void currentIndexChanged();

private:
    void onTrackChanged();
    void updateIndex();
    void setState(const QString &state);

    Catalog *m_catalog;
    Player *m_player;
    QString m_state = "none", m_videoId;
    QVariantList m_lines;
    QList<qint64> m_times;
    int m_index = -1;
};
