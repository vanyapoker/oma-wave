#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include "catalog.h"
#include "controlserver.h"
#include "lyrics.h"
#include "player.h"
#include "settings.h"
#include "theme.h"
#include "translator.h"
#include "visualizer.h"

class OmawaveTests : public QObject {
    Q_OBJECT

    QTemporaryDir m_runtime, m_media;
    QVariantList m_tracks;

    static void write(const QString &path, const QByteArray &contents) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }

private slots:
    void initTestCase() {
        // Keep sockets and config away from a running omawave.
        qputenv("XDG_RUNTIME_DIR", m_runtime.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", m_runtime.path().toUtf8());
        for (const char *name : {"a", "b", "c"}) {
            const QString file = m_media.filePath(QString(name) + ".ogg");
            QProcess ffmpeg;
            ffmpeg.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "sine=f=440:d=2", file});
            QVERIFY2(ffmpeg.waitForFinished(20000) && ffmpeg.exitCode() == 0, "ffmpeg is required for player tests");
            m_tracks << QVariantMap{{"title", name}, {"url", file}, {"videoId", name}};
        }
    }

    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding() {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n");
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        write(response, "{\"int\":0}");
        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        write(response, "{\"int\":6}");
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish() {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile() {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms() {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"}})");
        write(dir.path() + "/ru.json",
              R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
    }

    void lyricsIndexAt() {
        QCOMPARE(Lyrics::indexAt({0, 5000, 9000}, 4999), 0);
        QCOMPARE(Lyrics::indexAt({0, 5000, 9000}, 9000), 2);
        QCOMPARE(Lyrics::indexAt({1000}, 0), -1);
    }

    void visualizerParsesFrames() {
        QCOMPARE(Visualizer::parseLine("1;2;3;4;5;6;7;8;"), (QList<int>{1, 2, 3, 4, 5, 6, 7, 8}));
        QCOMPARE(Visualizer::parseLine("1;2;3;4;5;6;7;250;"), (QList<int>{1, 2, 3, 4, 5, 6, 7, 100}));
        QVERIFY(Visualizer::parseLine("1;2;3;").isEmpty());
        QVERIFY(Visualizer::parseLine("a;b;c;d;e;f;g;h;").isEmpty());
    }

    void settingsRoundTrip() {
        QTemporaryDir dir;
        {
            Settings s(dir.path());
            QCOMPARE(s.volume(), 70);
            QCOMPARE(s.browser(), QString("chromium"));
            s.setVolume(33);
            s.setBrowser("firefox");
        }
        Settings again(dir.path());
        QCOMPARE(again.volume(), 33);
        QCOMPARE(again.browser(), QString("firefox"));
        write(dir.path() + "/settings.json", "{not json");
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("corrupt settings"));
        Settings corrupt(dir.path());
        QCOMPARE(corrupt.volume(), 70);
    }

    void playerQueue() {
        Settings settings(m_runtime.path());
        Player player(nullptr, &settings, {}, {"--ao=null"});
        QVERIFY2(player.error().isEmpty(), qPrintable(player.error()));
        player.playTracks(m_tracks, 1, false);
        QTRY_COMPARE(player.queueIndex(), 1);
        QCOMPARE(player.track().value("title").toString(), QString("b"));
        QTRY_COMPARE(player.queue()->rowCount(), 3);

        player.removeAt(0);
        QTRY_COMPARE(player.queue()->rowCount(), 2);
        QTRY_COMPARE(player.queueIndex(), 0);

        player.next();
        QTRY_COMPARE(player.queueIndex(), 1);
        QTRY_COMPARE(player.track().value("title").toString(), QString("c"));

        player.moveTrack(1, 0);
        QTRY_COMPARE(player.queue()->row(0).value("title").toString(), QString("c"));
    }

    void controlProtocol() {
        Settings settings(m_runtime.path());
        Player player(nullptr, &settings, {}, {"--ao=null"});
        Lyrics lyrics(nullptr, &player);
        Visualizer visualizer("/nonexistent/cava");
        const QString path = m_runtime.filePath("test-control.sock");
        ControlServer server(&player, &lyrics, &visualizer, path);
        QVERIFY(server.listen());

        player.playTracks(m_tracks, 0, false);
        QTRY_COMPARE(player.queueCount(), 3);

        QLocalSocket client;
        client.connectToServer(path);
        QVERIFY(client.waitForConnected(2000));
        QList<QJsonObject> received;
        auto pump = [&] {
            while (client.canReadLine())
                received << QJsonDocument::fromJson(client.readLine()).object();
        };
        connect(&client, &QLocalSocket::readyRead, this, pump);
        QTRY_VERIFY(received.size() >= 2);
        QCOMPARE(received[0].value("type").toString(), QString("hello"));
        QCOMPARE(received[1].value("type").toString(), QString("state"));
        QCOMPARE(received[1].value("v").toInt(), 1);
        QCOMPARE(received[1].value("queueCount").toInt(), 3);

        received.clear();
        client.write("{\"v\":1,\"cmd\":\"bogus\"}\n");
        QTRY_VERIFY(!received.isEmpty());
        QCOMPARE(received.last().value("type").toString(), QString("error"));

        received.clear();
        client.write("{\"v\":1,\"cmd\":\"next\"}\n");
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                for (const auto &m : received)
                    if (m.value("type").toString() == "state" && m.value("queueIndex").toInt() == 1)
                        return true;
                return false;
            }(), 5000);
    }

    void catalogWorkerStops() {
        QTemporaryDir dir;
        const QString script = dir.filePath("worker.py");
        write(script,
              "import sys, json\n"
              "print(json.dumps({'id': 0, 'result': {'ready': True, 'signedIn': False}}), flush=True)\n"
              "req = json.loads(sys.stdin.readline())\n"
              "print(json.dumps({'id': req['id'], 'result': {'ok': 1}}), flush=True)\n"
              "sys.exit(1)\n");
        Catalog catalog(script, dir.path());
        bool first = false;
        catalog.request("ping", {}, [&](const QVariant &r, const QString &code, const QString &) {
            first = code.isEmpty() && r.toMap().value("ok").toInt() == 1;
        });
        QTRY_VERIFY(first);
        QTRY_VERIFY(!catalog.error().isEmpty());
        QString code;
        catalog.request("ping", {}, [&](const QVariant &, const QString &c, const QString &) { code = c; });
        QTRY_COMPARE(code, QString("worker_stopped"));
    }
};

QTEST_MAIN(OmawaveTests)
#include "omawave_tests.moc"
