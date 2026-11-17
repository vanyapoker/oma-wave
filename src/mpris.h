#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusContext>
#include <QDBusObjectPath>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

class Player;

// MPRIS 2 service so media keys and playerctl reach omawave.
class Mpris : public QObject, protected QDBusContext {
    Q_OBJECT
public:
    explicit Mpris(Player *player, QObject *parent = nullptr);

    Player *player() const { return m_player; }
    QVariantMap metadata() const;
    QString playbackStatus() const;
    QString currentTrackId() const;
    void replyNotSupported(const QString &what);

signals:
    void showRequested();
    void quitRequested();

private:
    void publish();

    Player *m_player;
    QVariantMap m_last;
};

class MprisRootAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes)
public:
    explicit MprisRootAdaptor(Mpris *mpris) : QDBusAbstractAdaptor(mpris), m_mpris(mpris) {}
    bool canQuit() const { return true; }
    bool canRaise() const { return true; }
    bool hasTrackList() const { return false; }
    QString identity() const { return "omawave"; }
    QString desktopEntry() const { return "omawave"; }
    QStringList supportedUriSchemes() const { return {}; }
    QStringList supportedMimeTypes() const { return {}; }
public slots:
    void Raise() { emit m_mpris->showRequested(); }
    void Quit() { emit m_mpris->quitRequested(); }
private:
    Mpris *m_mpris;
};

class MprisPlayerAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double MinimumRate READ rate)
    Q_PROPERTY(double MaximumRate READ rate)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
    Q_PROPERTY(bool CanPlay READ always)
    Q_PROPERTY(bool CanPause READ always)
    Q_PROPERTY(bool CanSeek READ always)
    Q_PROPERTY(bool CanControl READ always)
public:
    explicit MprisPlayerAdaptor(Mpris *mpris);
    QString playbackStatus() const { return m_mpris->playbackStatus(); }
    double rate() const { return 1.0; }
    void setRate(double) {}
    QVariantMap metadata() const { return m_mpris->metadata(); }
    double volume() const;
    void setVolume(double v);
    qlonglong position() const;
    bool canGoNext() const;
    bool canGoPrevious() const;
    bool always() const { return true; }
public slots:
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong offsetUs);
    void SetPosition(const QDBusObjectPath &trackId, qlonglong positionUs);
    void OpenUri(const QString &uri);
signals:
    void Seeked(qlonglong positionUs);
private:
    Mpris *m_mpris;
};
