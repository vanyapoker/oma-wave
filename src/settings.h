#pragma once

#include <QObject>
#include <QString>

// Persistent user settings, saved atomically on every change.
class Settings : public QObject {
    Q_OBJECT
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QString browser READ browser WRITE setBrowser NOTIFY browserChanged)

public:
    explicit Settings(const QString &directory = {}, QObject *parent = nullptr);

    int volume() const { return m_volume; }
    QString browser() const { return m_browser; }
    void setVolume(int volume);
    void setBrowser(const QString &browser);

signals:
    void volumeChanged();
    void browserChanged();

private:
    void load();
    void save();

    QString m_path;
    int m_volume = 70;
    QString m_browser = "chromium";
};
