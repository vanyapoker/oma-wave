#pragma once

#include <QHash>
#include <QObject>
#include <QProcess>
#include <QVariant>
#include <QElapsedTimer>
#include <functional>

// Client for the Python catalogue worker (catalog/catalog.py): JSON lines over
// stdin/stdout, one request id per call.
class Catalog : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList home READ home NOTIFY homeChanged)
    Q_PROPERTY(QVariantMap searchResults READ searchResults NOTIFY searchResultsChanged)
    Q_PROPERTY(QVariantMap detail READ detail NOTIFY detailChanged)
    Q_PROPERTY(QVariantMap library READ library NOTIFY libraryChanged)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY signedInChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    using Done = std::function<void(const QVariant &result, const QString &errorCode, const QString &message)>;

    explicit Catalog(const QString &workerPath = {}, const QString &configDir = {}, QObject *parent = nullptr);
    ~Catalog() override;

    QVariantList home() const { return m_home; }
    QVariantMap searchResults() const { return m_search; }
    QVariantMap detail() const { return m_detail; }
    QVariantMap library() const { return m_library; }
    bool signedIn() const { return m_signedIn; }
    bool busy() const { return m_pending.size() > 0; }
    QString error() const { return m_error; }

    int request(const QString &method, const QVariantMap &params, Done done);

    Q_INVOKABLE void loadHome();
    Q_INVOKABLE void search(const QString &query);
    Q_INVOKABLE void openDetail(const QString &kind, const QString &id);
    Q_INVOKABLE void loadLibrary();
    Q_INVOKABLE void signIn(const QString &browser);
    Q_INVOKABLE void signOut();
    Q_INVOKABLE void clearError();
    void reportError(const QString &message) { setError(message); }

    void upNext(const QString &videoId, Done done);
    void lyrics(const QString &title, const QString &artist, const QString &album, int durationSec, Done done);

    void shutdown();

signals:
    void homeChanged();
    void searchResultsChanged();
    void detailChanged();
    void libraryChanged();
    void signedInChanged();
    void busyChanged();
    void errorChanged();

private:
    void startWorker();
    void onReadyRead();
    void onFinished(int code);
    void handleLine(const QByteArray &line);
    void setError(const QString &message);
    void setSignedIn(bool value);
    void finish(int id, const QVariant &result, const QString &code, const QString &message);

    QString m_workerPath, m_configDir;
    QProcess m_process;
    QElapsedTimer m_started;
    QByteArray m_buffer, m_stderr;
    QHash<int, Done> m_pending;
    int m_nextId = 1;
    bool m_stopping = false, m_fatal = false;
    QVariantList m_home;
    QVariantMap m_search, m_detail, m_library;
    bool m_signedIn = false;
    QString m_error;
    int m_searchSerial = 0, m_detailSerial = 0;
};
