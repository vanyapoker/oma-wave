#pragma once

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QHash>
#include <QLocalSocket>
#include <QProcess>
#include <QTimer>
#include <QTemporaryDir>
#include <QVariantMap>

class Catalog;
class Settings;

// Mirror of mpv's playlist. It is rebuilt from the observed `playlist`
// property, so it cannot drift from what mpv will actually play.
class QueueModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { VideoId = Qt::UserRole + 1, Title, Artist, Album, Thumb, DurationMs, IsCurrent };

    explicit QueueModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_rows.size(); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRows(const QList<QVariantMap> &rows, int current);
    QVariantMap row(int i) const { return i >= 0 && i < m_rows.size() ? m_rows[i] : QVariantMap(); }
    int current() const { return m_current; }

private:
    QList<QVariantMap> m_rows;
    int m_current = -1;
};

class Player : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(bool buffering READ buffering NOTIFY bufferingChanged)
    Q_PROPERTY(QVariantMap track READ track NOTIFY trackChanged)
    Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY positionChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY durationChanged)
    Q_PROPERTY(int volume READ volume NOTIFY volumeChanged)
    Q_PROPERTY(int queueIndex READ queueIndex NOTIFY queueChanged)
    Q_PROPERTY(bool canNext READ canNext NOTIFY queueChanged)
    Q_PROPERTY(bool canPrevious READ canPrevious NOTIFY trackChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QueueModel *queue READ queue CONSTANT)

public:
    explicit Player(Catalog *catalog, Settings *settings, const QString &mpvPath = {},
                    const QStringList &extraArgs = {}, QObject *parent = nullptr);
    ~Player() override;

    bool playing() const { return !m_paused && !m_idle; }
    bool buffering() const { return m_buffering; }
    QVariantMap track() const { return m_queue.row(m_queue.current()); }
    qint64 positionMs() const { return m_positionMs; }
    qint64 durationMs() const { return m_durationMs; }
    int volume() const { return m_volume; }
    int queueIndex() const { return m_queue.current(); }
    int queueCount() const { return m_queue.rowCount(); }
    bool canNext() const { return queueIndex() >= 0 && queueIndex() < queueCount() - 1; }
    bool canPrevious() const { return queueIndex() >= 0; }
    QString error() const { return m_error; }
    QueueModel *queue() { return &m_queue; }
    bool idle() const { return m_idle; }

    static QString urlFor(const QVariantMap &track);

    Q_INVOKABLE void playTracks(const QVariantList &tracks, int start, bool radio);
    Q_INVOKABLE void enqueue(const QVariantMap &track);
    Q_INVOKABLE void playNext(const QVariantMap &track);
    Q_INVOKABLE void removeAt(int i);
    Q_INVOKABLE void moveTrack(int from, int to);
    Q_INVOKABLE void jump(int i);
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(qint64 ms);
    Q_INVOKABLE void setVolume(int volume);
    Q_INVOKABLE void clearError();

    void shutdown();
    void setAutoplay(bool on) { m_autoplay = on; }
    Q_INVOKABLE void appendTracks(const QVariantList &tracks);
    Q_INVOKABLE void playCollection(const QString &kind, const QString &id);

signals:
    void playingChanged();
    void bufferingChanged();
    void trackChanged();
    void positionChanged();
    void durationChanged();
    void volumeChanged();
    void queueChanged();
    void errorChanged();
    // Anything except the position changed; drives the control socket's state message.
    void stateChanged();
    void seeked(qint64 ms);
    // Emitted when playTracks replaces the whole queue.
    void queueReplaced();

private slots:
    void suspendMpv();

private:
    void startMpv();
    void connectSocket();
    void sendNow(const QVariantList &command);
    void finishRestore();
    void updateIdleTimer();
    void send(const QVariantList &command);
    void onData();
    void handleMessage(const QJsonObject &object);
    void onProperty(const QString &name, const QVariant &value, const QJsonValue &raw);
    void onMpvFinished();
    void setError(const QString &message);
    void applyCookies();
    void append(const QVariantMap &track);
    void remember(const QVariantMap &track);
    bool hasUrl(const QString &url) const;
    void appendUnique(const QVariantList &tracks);
    void maybeAutoplay();
    void updatePrefetch();
    void cancelPrefetch();
    void clearPrefetchMapping();
    void prefetchFinished(int exitCode, QProcess::ExitStatus status);
    void releaseRetiredPrefetch();

    Catalog *m_catalog;
    Settings *m_settings;
    QString m_mpvPath;
    QStringList m_extraArgs;
    QProcess m_mpv;
    QLocalSocket m_socket;
    QByteArray m_buffer;
    QList<QByteArray> m_outbox;
    bool m_connected = false;
    int m_connectAttempts = 0;
    QTimer m_retry;
    QTimer m_idleTimer;
    bool m_suspended = false;
    bool m_restoring = false;
    QElapsedTimer m_mpvStarted, m_positionClock;
    bool m_stopping = false;
    int m_requestId = 0;

    QueueModel m_queue;
    QHash<QString, QVariantMap> m_meta;
    QStringList m_urls;
    bool m_paused = true, m_idle = true, m_buffering = false;
    qint64 m_positionMs = 0, m_durationMs = 0;
    int m_volume = 70;
    QString m_error;
    QString m_autoplayFor;
    int m_playSerial = 0;
    bool m_autoplay = true;
    QTemporaryDir m_prefetchDir;
    QProcess m_prefetch;
    QTimer m_prefetchTimeout;
    QString m_prefetchUrl, m_prefetchFile, m_readyUrl, m_readyFile, m_failedPrefetchUrl;
    QStringList m_retiredPrefetch;
};
