#pragma once

#include <QObject>
#include <QString>

// Persistent user settings, saved atomically on every change.
class Settings : public QObject {
    Q_OBJECT
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QString browser READ browser WRITE setBrowser NOTIFY browserChanged)
    Q_PROPERTY(QString waveMood READ waveMood WRITE setWaveMood NOTIFY waveChanged)
    Q_PROPERTY(QString waveActivity READ waveActivity WRITE setWaveActivity NOTIFY waveChanged)
    Q_PROPERTY(QString waveCharacter READ waveCharacter WRITE setWaveCharacter NOTIFY waveChanged)

public:
    explicit Settings(const QString &directory = {}, QObject *parent = nullptr);

    int volume() const { return m_volume; }
    QString browser() const { return m_browser; }
    QString waveMood() const { return m_waveMood; }
    QString waveActivity() const { return m_waveActivity; }
    QString waveCharacter() const { return m_waveCharacter; }
    void setVolume(int volume);
    void setBrowser(const QString &browser);
    void setWaveMood(const QString &value);
    void setWaveActivity(const QString &value);
    void setWaveCharacter(const QString &value);

signals:
    void volumeChanged();
    void browserChanged();
    void waveChanged();

private:
    void load();
    void save();

    QString m_path;
    int m_volume = 70;
    QString m_browser = "chromium";
    QString m_waveMood = "any", m_waveActivity = "any", m_waveCharacter = "favourite";
};
