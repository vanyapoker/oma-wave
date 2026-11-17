#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>

class Catalog;
class Player;
class Settings;

// "My Wave": an endless personal stream. It keeps the player's queue topped up from the
// worker's `waveNext`, logs every listen (skip, partial, complete) and relays ratings.
class Wave : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString mood READ mood WRITE setMood NOTIFY settingsChanged)
    Q_PROPERTY(QString activity READ activity WRITE setActivity NOTIFY settingsChanged)
    Q_PROPERTY(QString character READ character WRITE setCharacter NOTIFY settingsChanged)
    Q_PROPERTY(QString rating READ rating NOTIFY ratingChanged) // "like" | "dislike" | "none"

public:
    Wave(Catalog *catalog, Player *player, Settings *settings, QObject *parent = nullptr);

    bool active() const { return m_active; }
    bool loading() const { return m_loading; }
    QString mood() const;
    QString activity() const;
    QString character() const;
    QString rating() const { return m_rating; }
    void setMood(const QString &value);
    void setActivity(const QString &value);
    void setCharacter(const QString &value);

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void rate(const QString &rating);
    Q_INVOKABLE void toggleLike();
    Q_INVOKABLE void dislike();

signals:
    void activeChanged();
    void loadingChanged();
    void settingsChanged();
    void ratingChanged();

private:
    QVariantMap params(const QVariantList &exclude, int count) const;
    void refill(bool replaceUpcoming);
    void topUp();
    void setActive(bool on);
    void setLoading(bool on);
    void setRating(const QString &rating);
    void onTrackChanged();
    void flushLog();
    void afterSettingChange(bool changed);

    Catalog *m_catalog;
    Player *m_player;
    Settings *m_settings;
    bool m_active = false, m_loading = false, m_starting = false, m_fetching = false;
    int m_serial = 0, m_exhaustedAt = -2;
    QTimer m_settle;
    QString m_rating = "none";

    QVariantMap m_logTrack;
    qint64 m_logMaxMs = 0, m_logDurationMs = 0;
    QString m_logSource = "other";
    bool m_interrupted = false;
};
