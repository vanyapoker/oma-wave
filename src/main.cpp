// omawave — YouTube Music player with an Omarchy bar widget.

#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTextStream>
#include <QSocketNotifier>
#include <QUrl>
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

#include "catalog.h"
#include "controlserver.h"
#include "lyrics.h"
#include "mpris.h"
#include "paths.h"
#include "player.h"
#include "settings.h"
#include "theme.h"
#include "translator.h"
#include "visualizer.h"
#include "wave.h"

namespace {
const QStringList kCliCommands = {"status", "play-pause", "next", "previous", "show", "quit"};

// Maps a CLI word to a control-protocol command.
QString protocolCommand(const QString &word) { return word == "play-pause" ? "playPause" : word; }

// Sends one command to the running instance. Returns the exit code.
int runClient(QLocalSocket &socket, const QString &command, bool json) {
    const QJsonObject message{{"v", 1}, {"cmd", protocolCommand(command)}};
    socket.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + "\n");
    socket.flush();
    if (command != "status") {
        socket.waitForBytesWritten(500);
        return 0;
    }
    QByteArray buffer;
    while (socket.waitForReadyRead(2000)) {
        buffer += socket.readAll();
        int nl;
        while ((nl = buffer.indexOf('\n')) >= 0) {
            const auto object = QJsonDocument::fromJson(buffer.left(nl)).object();
            buffer.remove(0, nl + 1);
            if (object.value("type").toString() != "state")
                continue;
            QTextStream out(stdout);
            if (json) {
                out << QJsonDocument(object).toJson(QJsonDocument::Compact) << "\n";
            } else if (object.value("track").isNull()) {
                out << "nothing playing\n";
            } else {
                const auto track = object.value("track").toObject();
                out << track.value("title").toString() << " — " << track.value("artist").toString() << " ("
                    << (object.value("playing").toBool() ? "playing" : "paused") << ")\n";
            }
            return 0;
        }
    }
    QTextStream(stderr) << "omawave did not answer\n";
    return 1;
}

// SIGTERM (systemd stop) and SIGINT must reach the normal quit path, or mpv would be orphaned.
int g_signalPipe[2];
void onSignal(int) {
    const char byte = 1;
    [[maybe_unused]] auto r = ::write(g_signalPipe[1], &byte, 1);
}
}

int main(int argc, char *argv[]) {
    QStringList args;
    for (int i = 1; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);
    const bool background = args.contains("--background");
    const bool json = args.contains("--json");
    QString command;
    for (const auto &a : args)
        if (kCliCommands.contains(a))
            command = a;

    // Commands never show a window, so they run without a display.
    if (!command.isEmpty())
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QGuiApplication app(argc, argv);
    app.setApplicationName("omawave");
    app.setApplicationVersion("0.1.0");
    app.setDesktopFileName("omawave");
    app.setWindowIcon(QIcon::fromTheme("omawave"));

    // The running instance owns playback; any other invocation just talks to it.
    QLocalSocket probe;
    probe.connectToServer(Paths::controlSocket());
    if (probe.waitForConnected(300)) {
        if (background)
            return 0;
        return runClient(probe, command.isEmpty() ? "show" : command, json);
    }
    if (!command.isEmpty()) {
        QTextStream(stderr) << "omawave is not running\n";
        return 1;
    }

    app.setQuitOnLastWindowClosed(false);
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_signalPipe) == 0) {
        QSocketNotifier *notifier = new QSocketNotifier(g_signalPipe[0], QSocketNotifier::Read, &app);
        std::signal(SIGTERM, onSignal);
        std::signal(SIGINT, onSignal);
        QObject::connect(notifier, &QSocketNotifier::activated, &app, [] {
            QMetaObject::invokeMethod(QCoreApplication::instance(), "exit", Qt::QueuedConnection, Q_ARG(int, 0));
        });
    }
    QQuickStyle::setStyle("Material");

    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    Settings settings;
    Catalog catalog;
    Player player(&catalog, &settings);
    Lyrics lyrics(&catalog, &player);
    Wave wave(&catalog, &player, &settings);
    Visualizer visualizer;
    ControlServer server(&player, &lyrics, &visualizer, &wave);
    if (!server.listen())
        return 1;
    Mpris mpris(&player);

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    auto *ctx = engine.rootContext();
    ctx->setContextProperty("translator", &translator);
    ctx->setContextProperty("theme", &theme);
    ctx->setContextProperty("settings", &settings);
    ctx->setContextProperty("catalog", &catalog);
    ctx->setContextProperty("player", &player);
    ctx->setContextProperty("lyrics", &lyrics);
    ctx->setContextProperty("wave", &wave);
    ctx->setContextProperty("visualizer", &visualizer);
    ctx->setContextProperty("startHidden", background);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());

    auto show = [window] { QMetaObject::invokeMethod(window, "present"); };
    QObject::connect(&server, &ControlServer::showRequested, window, show);
    QObject::connect(&mpris, &Mpris::showRequested, window, show);
    auto quit = [window] { QMetaObject::invokeMethod(window, "quitApp"); };
    QObject::connect(&server, &ControlServer::quitRequested, window, quit);
    QObject::connect(&mpris, &Mpris::quitRequested, window, quit);

    // cava only runs while something can show its output.
    auto updateVisualizer = [&] {
        visualizer.setWanted(player.playing() && (server.levelSubscribers() > 0 || window->isVisible()));
    };
    QObject::connect(&player, &Player::playingChanged, &app, updateVisualizer);
    QObject::connect(&server, &ControlServer::levelSubscribersChanged, &app, updateVisualizer);
    QObject::connect(window, &QWindow::visibleChanged, &app, updateVisualizer);

    QObject::connect(&app, &QGuiApplication::aboutToQuit, &app, [&] {
        visualizer.setWanted(false);
        player.shutdown();
        catalog.shutdown();
    });
    return app.exec();
}
