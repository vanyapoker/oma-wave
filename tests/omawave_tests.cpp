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
#include "paths.h"
#include "settings.h"
#include "theme.h"
#include "translator.h"
#include "visualizer.h"
#include "wave.h"

class OmawaveTests : public QObject {
    Q_OBJECT

    QTemporaryDir m_runtime, m_media;
    QVariantList m_tracks;

    static void write(const QString &path, const QByteArray &contents) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }

    void previewWorker(const QString &dir, const QString &mode) {
        for (int i = 0; i < 30; ++i)
            QVERIFY(QFile::copy(m_media.filePath("a.ogg"), dir + QString("/%1.ogg").arg(i)));
        qputenv("OMAWAVE_PREVIEW_DIR", dir.toUtf8());
        qputenv("OMAWAVE_PREVIEW_MODE", mode.toUtf8());
        write(dir + "/worker.py",
              "import sys, json, os, time\n"
              "d=os.environ['OMAWAVE_PREVIEW_DIR']; mode=os.environ['OMAWAVE_PREVIEW_MODE']; n=0\n"
              "print(json.dumps({'id':0,'result':{'ready':True,'signedIn':False}}),flush=True)\n"
              "for line in sys.stdin:\n"
              " r=json.loads(line); result={}\n"
              " if r['method']=='waveNext':\n"
              "  n+=1; p=r['params']; open(d+'/requests','a').write(json.dumps(p)+'\\n')\n"
              "  if mode=='hold' and n==1:\n"
              "   while not os.path.exists(d+'/release'): time.sleep(.01)\n"
              "  if mode=='error' and n==1:\n"
              "   print(json.dumps({'id':r['id'],'error':{'code':'failed','message':'Preview failed'}}),flush=True); continue\n"
              "  count=2 if mode=='error' else 6\n"
              "  result={'tracks':[{'videoId':str(n)+'-'+str(i),'title':p['mood']+' '+str(i),'url':d+'/'+str(((n-1)*6+i)%30)+'.ogg'} for i in range(count)]}\n"
              " print(json.dumps({'id':r['id'],'result':result}),flush=True)\n");
    }

    static QList<QJsonObject> previewRequests(const QString &dir) {
        QFile file(dir + "/requests");
        QList<QJsonObject> requests;
        if (file.open(QIODevice::ReadOnly))
            while (!file.atEnd()) requests << QJsonDocument::fromJson(file.readLine()).object();
        return requests;
    }

    static int previewRequestCount(const QString &dir, const QString &mood, const QString &character) {
        int count = 0;
        for (const auto &request : previewRequests(dir))
            if (request.value("mood").toString() == mood &&
                request.value("character").toString() == character &&
                request.value("exclude").toArray().isEmpty())
                ++count;
        return count;
    }

    static int topUpRequestCount(const QString &dir, const QString &mood, const QString &character) {
        int count = 0;
        for (const auto &request : previewRequests(dir))
            if (request.value("mood").toString() == mood &&
                request.value("character").toString() == character &&
                !request.value("exclude").toArray().isEmpty())
                ++count;
        return count;
    }

    static QVariantMap mixCard(const Wave &wave, const QString &character) {
        for (const auto &card : wave.mixCards())
            if (card.toMap().value("character").toString() == character)
                return card.toMap();
        return {};
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
        QCOMPARE(again.waveMood(), QString("any"));
        again.setWaveMood("calm");
        again.setWaveMood("bogus");
        QCOMPARE(again.waveMood(), QString("calm"));
        QCOMPARE(Settings(dir.path()).waveMood(), QString("calm"));
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

    void pausedQueueSurvivesMpvSuspension() {
        Settings settings(m_runtime.path());
        Player player(nullptr, &settings, {}, {"--ao=null"});
        player.playTracks(m_tracks, 1, false);
        QTRY_COMPARE(player.queueIndex(), 1);
        QTRY_VERIFY(player.playing());
        player.seek(900);
        QTRY_VERIFY(player.positionMs() >= 500);
        player.togglePause();
        QTRY_VERIFY(!player.playing());
        const qint64 pausedAt = player.positionMs();
        QVERIFY(QMetaObject::invokeMethod(&player, "suspendMpv"));
        auto mpvStopped = [] {
            QLocalSocket socket;
            socket.connectToServer(Paths::mpvSocket());
            return !socket.waitForConnected(50);
        };
        QTRY_VERIFY(mpvStopped());
        QCOMPARE(player.queueCount(), 3);
        QCOMPARE(player.track().value("title").toString(), QString("b"));
        QCOMPARE(player.positionMs(), pausedAt);
        QVERIFY(player.error().isEmpty());

        // Queue edits wake mpv but do not resume playback.
        player.moveTrack(0, 2);
        QTRY_COMPARE(player.queueIndex(), 0);
        QCOMPARE(player.track().value("title").toString(), QString("b"));
        QVERIFY(!player.playing());
        QTRY_VERIFY(player.positionMs() >= 500);
        player.togglePause();
        QTRY_VERIFY(player.playing());
        QCOMPARE(player.queueCount(), 3);
    }

    void resumeWhileMpvIsStopping() {
        Settings settings(m_runtime.path());
        Player player(nullptr, &settings, {}, {"--ao=null"});
        player.playTracks(m_tracks, 0, false);
        QTRY_VERIFY(player.playing());
        player.togglePause();
        QTRY_VERIFY(!player.playing());
        QVERIFY(QMetaObject::invokeMethod(&player, "suspendMpv"));
        player.togglePause();
        QTRY_VERIFY(player.playing());
        QCOMPARE(player.queueCount(), 3);
        QCOMPARE(player.track().value("title").toString(), QString("a"));
        QVERIFY(player.error().isEmpty());
    }

    void controlProtocol() {
        Settings settings(m_runtime.path());
        Player player(nullptr, &settings, {}, {"--ao=null"});
        Lyrics lyrics(nullptr, &player);
        Visualizer visualizer("/nonexistent/cava");
        const QString path = m_runtime.filePath("test-control.sock");
        Wave wave(nullptr, &player, &settings);
        ControlServer server(&player, &lyrics, &visualizer, &wave, path);
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
        QCOMPARE(received[1].value("wave").toObject().value("active").toBool(), false);
        QCOMPARE(received[1].value("rating").toString(), QString("none"));

        received.clear();
        client.write("{\"v\":1,\"cmd\":\"rate\",\"rating\":\"bogus\"}\n");
        QTRY_VERIFY(!received.isEmpty());
        QCOMPARE(received.last().value("type").toString(), QString("error"));

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

        player.togglePause();
        QTRY_VERIFY(!player.playing());
        received.clear();
        client.write("{\"v\":1,\"cmd\":\"seek\",\"positionMs\":1200}\n");
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                for (const auto &m : received)
                    if (m.value("type").toString() == "position" && m.value("positionMs").toInt() >= 1000)
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

    void waveStartsAndTopsUp() {
        QTemporaryDir dir;
        QStringList media;
        for (int i = 0; i < 9; ++i) {
            media << dir.filePath(QString("w%1.ogg").arg(i));
            QVERIFY(QFile::copy(m_media.filePath("a.ogg"), media.last()));
        }
        qputenv("OMAWAVE_TEST_MEDIA", media.join(':').toUtf8());
        const QString script = dir.filePath("worker.py");
        write(script,
              "import sys, json, os\n"
              "media = os.environ['OMAWAVE_TEST_MEDIA'].split(':')\n"
              "n = 0\n"
              "print(json.dumps({'id': 0, 'result': {'ready': True, 'signedIn': False}}), flush=True)\n"
              "for line in sys.stdin:\n"
              "    req = json.loads(line)\n"
              "    result = {}\n"
              "    if req['method'] == 'waveNext':\n"
              "        tracks = []\n"
              "        for _ in range(3):\n"
              "            tracks.append({'videoId': 'w%d' % n, 'title': 'w%d' % n, 'url': media[n % len(media)]})\n"
              "            n += 1\n"
              "        result = {'tracks': tracks}\n"
              "    print(json.dumps({'id': req['id'], 'result': result}), flush=True)\n");
        Catalog catalog(script, dir.path());
        Settings settings(dir.path());
        Player player(&catalog, &settings, {}, {"--ao=null"});
        Wave wave(&catalog, &player, &settings);

        wave.start();
        QTRY_VERIFY(wave.active());
        // Three tracks leave only two upcoming, so the wave fetches another batch at once.
        QTRY_COMPARE(player.queueCount(), 6);
        player.jump(4);
        QTRY_COMPARE(player.queueCount(), 9);
        player.playTracks(m_tracks, 0, false);
        QTRY_VERIFY(!wave.active());
    }

    void wavePreviewPlaysExactBatch() {
        QTemporaryDir dir;
        previewWorker(dir.path(), "normal");
        Catalog catalog(dir.filePath("worker.py"), dir.path());
        Settings settings(dir.path());
        Player player(&catalog, &settings, {}, {"--ao=null"});
        Wave wave(&catalog, &player, &settings);
        wave.preparePreview();
        QTRY_COMPARE(wave.previewTracks().size(), 3);
        QVERIFY(!wave.active());
        QCOMPARE(player.queueCount(), 0);
        const QVariantList preview = wave.previewTracks();
        wave.start();
        QTRY_COMPARE(player.queueCount(), 6);
        for (int i = 0; i < 3; ++i)
            QCOMPARE(player.queue()->row(i).value("videoId"), preview[i].toMap().value("videoId"));
        QCOMPARE(previewRequestCount(dir.path(), wave.mood(), wave.character()), 1);
    }

    void waveCardSelectionPlaysCachedPreview() {
        QTemporaryDir dir;
        previewWorker(dir.path(), "normal");
        Catalog catalog(dir.filePath("worker.py"), dir.path());
        Settings settings(dir.path());
        Player player(&catalog, &settings, {}, {"--ao=null"});
        Wave wave(&catalog, &player, &settings);
        wave.prepareMixCards();
        QTRY_COMPARE(mixCard(wave, "favourite").value("tracks").toList().size(), 3);
        QTRY_COMPARE(mixCard(wave, "discover").value("tracks").toList().size(), 3);
        QTRY_COMPARE(mixCard(wave, "popular").value("tracks").toList().size(), 3);
        const QVariantList favourite = mixCard(wave, "favourite").value("tracks").toList();
        const QVariantList discover = mixCard(wave, "discover").value("tracks").toList();

        wave.playMix("discover");
        QTRY_COMPARE(player.queueCount(), 6);
        for (int i = 0; i < 3; ++i)
            QCOMPARE(player.queue()->row(i).value("videoId"), discover[i].toMap().value("videoId"));
        QCOMPARE(previewRequestCount(dir.path(), wave.mood(), "discover"), 1);
        QCOMPARE(mixCard(wave, "favourite").value("tracks").toList(), favourite);

        wave.playMix("favourite");
        QTRY_COMPARE(player.queue()->row(0).value("videoId"), favourite[0].toMap().value("videoId"));
        for (int i = 0; i < 3; ++i)
            QTRY_COMPARE(player.queue()->row(i).value("videoId"), favourite[i].toMap().value("videoId"));
        QCOMPARE(previewRequestCount(dir.path(), wave.mood(), "favourite"), 1);
    }

    void waveTuningWaitsForPlay() {
        QTemporaryDir dir;
        previewWorker(dir.path(), "normal");
        Catalog catalog(dir.filePath("worker.py"), dir.path());
        Settings settings(dir.path());
        Player player(&catalog, &settings, {}, {"--ao=null"});
        Wave wave(&catalog, &player, &settings);
        wave.start();
        QTRY_COMPARE(player.queueCount(), 6);
        wave.setMood("calm");
        wave.preparePreview();
        QTRY_COMPARE(wave.previewTracks().size(), 3);
        QCOMPARE(player.queue()->row(0).value("videoId").toString(), QString("1-0"));
        QCOMPARE(player.queue()->row(5).value("videoId").toString(), QString("1-5"));
        player.jump(4);
        QTRY_COMPARE(topUpRequestCount(dir.path(), "any", "favourite"), 1);
        const QString first = wave.previewTracks()[0].toMap().value("videoId").toString();
        wave.start();
        QTRY_COMPARE(player.queue()->row(0).value("videoId").toString(), first);
        player.jump(4);
        QTRY_COMPARE(topUpRequestCount(dir.path(), "calm", "favourite"), 1);
    }

    void wavePreviewDiscardsStaleReplies() {
        QTemporaryDir dir;
        previewWorker(dir.path(), "hold");
        Catalog catalog(dir.filePath("worker.py"), dir.path());
        Settings settings(dir.path());
        Player player(&catalog, &settings, {}, {"--ao=null"});
        Wave wave(&catalog, &player, &settings);
        wave.start();
        QTRY_COMPARE(previewRequestCount(dir.path(), "any", "favourite"), 1);
        wave.setMood("happy");
        wave.setMood("calm");
        wave.start();
        wave.preparePreview();
        wave.stop();
        write(dir.filePath("release"), "go");
        QTRY_COMPARE(wave.previewTracks().size(), 3);
        QCOMPARE(wave.previewTracks()[0].toMap().value("title").toString(), QString("calm 0"));
        QCOMPARE(previewRequestCount(dir.path(), "happy", "favourite"), 0);
        QCOMPARE(previewRequestCount(dir.path(), "calm", "favourite"), 1);
        for (const auto &track : wave.previewTracks())
            QVERIFY(track.toMap().value("title").toString().startsWith("calm "));
        QCOMPARE(previewRequestCount(dir.path(), "any", "favourite"), 1);
        QVERIFY(!wave.active());
        QCOMPARE(player.queueCount(), 0);
    }

    void wavePreviewRetriesFailures() {
        QTemporaryDir dir;
        previewWorker(dir.path(), "error");
        Catalog catalog(dir.filePath("worker.py"), dir.path());
        Settings settings(dir.path());
        Player player(&catalog, &settings, {}, {"--ao=null"});
        Wave wave(&catalog, &player, &settings);
        wave.preparePreview();
        QTRY_COMPARE(wave.previewError(), QString("Preview failed"));
        QVERIFY(!wave.active());
        QCOMPARE(player.queueCount(), 0);
        wave.preparePreview();
        QTest::qWait(300);
        QCOMPARE(previewRequestCount(dir.path(), wave.mood(), wave.character()), 1);
        wave.retryPreview();
        QTRY_COMPARE(wave.previewTracks().size(), 2);
        QCOMPARE(previewRequestCount(dir.path(), wave.mood(), wave.character()), 2);
        const auto preview = wave.previewTracks();
        wave.start();
        QTRY_VERIFY(player.queueCount() >= 2);
        QCOMPARE(player.queue()->row(0).value("videoId"), preview[0].toMap().value("videoId"));
        QCOMPARE(player.queue()->row(1).value("videoId"), preview[1].toMap().value("videoId"));
    }
};

QTEST_MAIN(OmawaveTests)
#include "omawave_tests.moc"
