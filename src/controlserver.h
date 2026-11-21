#pragma once

#include <QJsonObject>
#include <QList>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QTimer>

class Player;
class Lyrics;
class Visualizer;
class Wave;

// Newline-delimited JSON control protocol v1 on a per-user local socket. It serves
// the bar plugin and the CLI.
class ControlServer : public QObject {
    Q_OBJECT
public:
    ControlServer(Player *player, Lyrics *lyrics, Visualizer *visualizer, Wave *wave, const QString &socketPath = {},
                  QObject *parent = nullptr);

    bool listen();
    int levelSubscribers() const;
    QJsonObject stateJson() const;

signals:
    void showRequested();
    void quitRequested();
    void levelSubscribersChanged();

private:
    struct Client {
        QLocalSocket *socket;
        QByteArray buffer;
        bool levels = false;
    };

    void onNewConnection();
    void onData(Client *client);
    void handle(Client *client, const QJsonObject &message);
    void sendTo(QLocalSocket *socket, const QJsonObject &object);
    void broadcast(const QJsonObject &object, bool onlyLevelClients = false);
    void broadcastState();

    Player *m_player;
    Lyrics *m_lyrics;
    Visualizer *m_visualizer;
    Wave *m_wave;
    QString m_path;
    QList<Client *> m_clients;
    QLocalServer m_server;
    QTimer m_debounce, m_positionTimer;
};
