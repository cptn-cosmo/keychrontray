#include "gamedetector.h"
#include "gamerules.h"
#include "processscanner.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {
GameConfig config()
{
    auto c = GameConfig::fromJson(R"({
        "fallbackProfile": "steam-generic",
        "ignoredSteamAppIds": [1493710, 250820],
        "games": [
            {"id": "ffxiv", "name": "FFXIV", "steamAppIds": [39210], "processNames": ["ffxiv_dx11.exe"], "profile": "ffxiv-dawntrail"},
            {"id": "wow", "name": "WoW", "processNames": ["Wow*.exe"], "profile": "wow-forever"},
            {"id": "bg3", "name": "BG3", "steamAppIds": [1086940], "profile": "bg3"},
            {"id": "control", "name": "Control", "steamAppIds": [870780], "profile": "control", "priority": 5}
        ]})");
    Q_ASSERT(c);
    return *c;
}
RunningProcess proc(const QString &name, quint32 appId = 0, qint64 start = 100, const QString &comm = QString())
{
    RunningProcess p;
    p.name = name;
    p.comm = comm.isEmpty() ? name.left(15) : comm;
    p.steamAppId = appId;
    p.startTime = start;
    return p;
}
}

class TestMatcher : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void noGame()
    {
        QVERIFY(!GameMatcher::match(config(), {proc(QStringLiteral("plasmashell")), proc(QStringLiteral("firefox"))}));
    }

    void matchBySteamAppId()
    {
        // Steam reaper: name is "reaper", app id from cmdline
        auto m = GameMatcher::match(config(), {proc(QStringLiteral("reaper"), 1086940)});
        QVERIFY(m);
        QCOMPARE(m->profile, QStringLiteral("bg3"));
        QVERIFY(!m->fallback);
    }

    void matchByProcessNameWildcardCaseInsensitive()
    {
        auto m = GameMatcher::match(config(), {proc(QStringLiteral("WOWCLASSIC.EXE"))});
        QVERIFY(m);
        QCOMPARE(m->ruleId, QStringLiteral("wow"));
        QVERIFY(!GameMatcher::match(config(), {proc(QStringLiteral("NotWow.exe"))}));
    }

    void ffxivByExeWithoutSteam()
    {
        auto m = GameMatcher::match(config(), {proc(QStringLiteral("ffxiv_dx11.exe"))});
        QVERIFY(m);
        QCOMPARE(m->profile, QStringLiteral("ffxiv-dawntrail"));
    }

    void fallbackForUnknownSteamGame()
    {
        auto m = GameMatcher::match(config(), {proc(QStringLiteral("reaper"), 4242)});
        QVERIFY(m);
        QVERIFY(m->fallback);
        QCOMPARE(m->profile, QStringLiteral("steam-generic"));
        QCOMPARE(m->steamAppId, 4242u);
    }

    void nonSteamShortcutsSkipFallback()
    {
        // Seen on real hardware: Battle.net launcher added to Steam as a shortcut, AppId=3259692658
        QVERIFY(!GameMatcher::match(config(), {proc(QStringLiteral("reaper"), 3259692658u)}));
        // ...but the game inside it still matches by process name
        auto m = GameMatcher::match(config(), {proc(QStringLiteral("reaper"), 3259692658u), proc(QStringLiteral("Wow.exe"), 3259692658u, 200)});
        QVERIFY(m);
        QCOMPARE(m->ruleId, QStringLiteral("wow"));
        // ...and a shortcut id can be mapped explicitly
        auto c = config();
        c.find(QStringLiteral("bg3"))->steamAppIds << 3259692658u;
        QCOMPARE(GameMatcher::match(c, {proc(QStringLiteral("reaper"), 3259692658u)})->ruleId, QStringLiteral("bg3"));
    }

    void ignoredToolIdsDoNotTrigger()
    {
        QVERIFY(!GameMatcher::match(config(), {proc(QStringLiteral("reaper"), 250820)}));
    }

    void noFallbackWhenDisabled()
    {
        auto c = config();
        c.fallbackProfile.clear();
        QVERIFY(!GameMatcher::match(c, {proc(QStringLiteral("reaper"), 4242)}));
    }

    void ruleBeatsFallbackEvenIfOlder()
    {
        auto m = GameMatcher::match(config(), {proc(QStringLiteral("reaper"), 39210, 100), proc(QStringLiteral("reaper"), 4242, 999)});
        QCOMPARE(m->ruleId, QStringLiteral("ffxiv"));
    }

    void newestWinsAtEqualPriority()
    {
        auto m = GameMatcher::match(config(), {proc(QStringLiteral("reaper"), 39210, 100), proc(QStringLiteral("reaper"), 1086940, 200)});
        QCOMPARE(m->ruleId, QStringLiteral("bg3"));
        m = GameMatcher::match(config(), {proc(QStringLiteral("reaper"), 39210, 300), proc(QStringLiteral("reaper"), 1086940, 200)});
        QCOMPARE(m->ruleId, QStringLiteral("ffxiv"));
    }

    void higherPriorityWins()
    {
        auto m = GameMatcher::match(config(), {proc(QStringLiteral("reaper"), 870780, 100), proc(QStringLiteral("reaper"), 1086940, 500)});
        QCOMPARE(m->ruleId, QStringLiteral("control"));
    }

    void deterministicTieBreak()
    {
        auto a = GameMatcher::match(config(), {proc(QStringLiteral("reaper"), 39210, 100), proc(QStringLiteral("reaper"), 1086940, 100)});
        auto b = GameMatcher::match(config(), {proc(QStringLiteral("reaper"), 1086940, 100), proc(QStringLiteral("reaper"), 39210, 100)});
        QCOMPARE(a->ruleId, b->ruleId);
        QCOMPARE(a->ruleId, QStringLiteral("bg3"));
    }

    void cmdlineAndEnvironParsing()
    {
        QCOMPARE(ProcessScanner::appIdFromCmdline({QStringLiteral("/home/u/.steam/ubuntu12_32/reaper"), QStringLiteral("SteamLaunch"),
                                                   QStringLiteral("AppId=39210"), QStringLiteral("--"), QStringLiteral("proton")}),
                 39210u);
        // An "AppId=" after "--" belongs to the game's own args
        QCOMPARE(ProcessScanner::appIdFromCmdline({QStringLiteral("game"), QStringLiteral("--"), QStringLiteral("AppId=5")}), 0u);
        QCOMPARE(ProcessScanner::appIdFromEnviron(QByteArray("HOME=/h\0SteamAppId=1245620\0X=1", 30)), 1245620u);
        QCOMPARE(ProcessScanner::appIdFromEnviron(QByteArray("SteamAppId=0\0SteamGameId=870780", 31)), 870780u);
        QCOMPARE(ProcessScanner::appIdFromEnviron(QByteArray("SteamGameId=12345678901234", 26)), 0u);
        QCOMPARE(ProcessScanner::baseName(QStringLiteral("Z:\\games\\wow\\Wow.exe")), QStringLiteral("Wow.exe"));
        QCOMPARE(ProcessScanner::baseName(QStringLiteral("/usr/bin/steam")), QStringLiteral("steam"));
    }

    void scanFakeProc()
    {
        QTemporaryDir dir;
        auto mk = [&](const QString &pid, const QByteArray &cmdline, const QByteArray &comm, const QByteArray &environ, const QByteArray &stat) {
            QDir().mkpath(dir.filePath(pid));
            for (auto [n, d] : {std::pair{QStringLiteral("cmdline"), cmdline}, {QStringLiteral("comm"), comm}, {QStringLiteral("environ"), environ},
                                {QStringLiteral("stat"), stat}}) {
                QFile f(dir.filePath(pid + QLatin1Char('/') + n));
                QVERIFY(f.open(QIODevice::WriteOnly));
                f.write(d);
            }
        };
        const QByteArray statTail = " S 1 1 1 0 -1 0 0 0 0 0 0 0 0 0 20 0 1 0 777 0 0";
        mk(QStringLiteral("10"), QByteArray("reaper\0SteamLaunch\0AppId=1086940\0--\0x\0", 38), "reaper\n", {}, "10 (reaper)" + statTail);
        mk(QStringLiteral("11"), QByteArray("C:\\bg3\\bg3.exe\0", 15), "bg3.exe\n", QByteArray("SteamAppId=1086940\0", 19), "11 (bg3 (x) y)" + statTail);
        mk(QStringLiteral("12"), {}, {}, {}, {}); // kernel thread
        auto procs = ProcessScanner::scan(dir.path());
        std::sort(procs.begin(), procs.end(), [](auto &a, auto &b) {
            return a.pid < b.pid;
        });
        QCOMPARE(procs.size(), 2);
        QCOMPARE(procs[0].steamAppId, 1086940u);
        QCOMPARE(procs[0].startTime, 777);
        QCOMPARE(procs[1].name, QStringLiteral("bg3.exe"));
        QCOMPARE(procs[1].steamAppId, 1086940u);
        QCOMPARE(procs[1].startTime, 777); // comm containing ") " parsed correctly
    }

    void detectorEmitsOnChangeOnly()
    {
        QTemporaryDir dir;
        QFile f(dir.filePath(QStringLiteral("games.json")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(config().toJson());
        f.close();
        GameDetector d(f.fileName());
        QCOMPARE(d.config().games.size(), 4);
        QList<RunningProcess> procs;
        d.setScanner([&] {
            return procs;
        });
        d.setNameLookup([](quint32 id) {
            return id == 4242 ? QStringLiteral("Some Game") : QString();
        });
        QSignalSpy spy(&d, &GameDetector::gameChanged);
        d.setEnabled(true);
        QCOMPARE(spy.count(), 0);
        procs = {proc(QStringLiteral("reaper"), 4242)};
        d.scanNow();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(d.current()->label, QStringLiteral("Some Game"));
        d.scanNow();
        QCOMPARE(spy.count(), 1);
        procs.clear();
        d.scanNow();
        QCOMPARE(spy.count(), 2);
        QVERIFY(!d.current());
    }

    void configRoundTripAndErrors()
    {
        auto c = config();
        auto c2 = GameConfig::fromJson(c.toJson());
        QVERIFY(c2);
        QCOMPARE(c2->games.size(), c.games.size());
        QCOMPARE(c2->ignoredSteamAppIds, c.ignoredSteamAppIds);
        QCOMPARE(c2->find(QStringLiteral("wow"))->processNames, QStringList{QStringLiteral("Wow*.exe")});
        QString err;
        QVERIFY(!GameConfig::fromJson(R"({"games":[{"id":"a"},{"id":"a"}]})", &err));
        QVERIFY(err.contains(QStringLiteral("duplicate")));
        QVERIFY(!GameConfig::fromJson(R"({"games":[{"id":"a","steamAppIds":["x"]}]})", &err));
    }
};

QTEST_GUILESS_MAIN(TestMatcher)
#include "test_matcher.moc"
