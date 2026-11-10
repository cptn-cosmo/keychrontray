#include "gamerules.h"
#include "profilemanager.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestProfile : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void parseHexColor()
    {
        QString err;
        auto p = Profile::fromJson(QStringLiteral("x"), "{\"name\":\"X\",\"effect\":\"Solid Color\",\"color\":\"#ff0000\",\"brightness\":50,\"speed\":10}", &err);
        QVERIFY2(p, qPrintable(err));
        const LedState s = p->ledState();
        QCOMPARE(s.effect, 1);
        QCOMPARE(s.hue, 0);
        QCOMPARE(s.saturation, 255);
        QCOMPARE(s.brightness, 128); // 50% of 255, rounded
        QCOMPARE(s.speed, 10);
    }

    void parseHueObjectAndEffectNumber()
    {
        auto p = Profile::fromJson(QStringLiteral("x"), "{\"effect\":2,\"color\":{\"hue\":170,\"saturation\":200}}");
        QVERIFY(p);
        QCOMPARE(p->ledState().effect, 2);
        QCOMPARE(p->ledState().hue, 170);
        QCOMPARE(p->ledState().saturation, 200);
        QCOMPARE(p->name, QStringLiteral("x")); // name defaults to id
    }

    void effectNamesAreLenient()
    {
        QCOMPARE(Effects::parse(QStringLiteral("solid_color")).value(), 1);
        QCOMPARE(Effects::parse(QStringLiteral("TYPING heatmap")).value(), 16);
        QCOMPARE(Effects::parse(QStringLiteral("22")).value(), 22);
        QVERIFY(!Effects::parse(QStringLiteral("disco")));
        QVERIFY(!Effects::parse(QStringLiteral("300")));
    }

    void rejectsBadInput_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("expect");
        QTest::newRow("syntax") << QByteArray("{") << QStringLiteral("JSON error");
        QTest::newRow("array") << QByteArray("[]") << QStringLiteral("object");
        QTest::newRow("effect") << QByteArray("{\"effect\":\"disco\"}") << QStringLiteral("unknown effect");
        QTest::newRow("color") << QByteArray("{\"color\":\"#zz\"}") << QStringLiteral("invalid color");
        QTest::newRow("hue") << QByteArray("{\"color\":{\"hue\":300}}") << QStringLiteral("0-255");
        QTest::newRow("brightness") << QByteArray("{\"brightness\":150}") << QStringLiteral("brightness");
        QTest::newRow("brightness-type") << QByteArray("{\"brightness\":\"high\"}") << QStringLiteral("brightness");
        QTest::newRow("speed") << QByteArray("{\"speed\":-1}") << QStringLiteral("speed");
    }
    void rejectsBadInput()
    {
        QFETCH(QByteArray, json);
        QFETCH(QString, expect);
        QString err;
        QVERIFY(!Profile::fromJson(QStringLiteral("x"), json, &err));
        QVERIFY2(err.contains(expect), qPrintable(err));
    }

    void rejectsBadIds()
    {
        QVERIFY(!Profile::fromJson(QStringLiteral("Has Space"), "{}"));
        QVERIFY(!Profile::fromJson(QStringLiteral("../evil"), "{}"));
        QVERIFY(Profile::isValidId(QStringLiteral("ffxiv-endwalker")));
    }

    void roundTrip()
    {
        Profile p;
        p.id = QStringLiteral("rt");
        p.name = QStringLiteral("Round trip");
        p.group = QStringLiteral("G");
        p.effect = 2;
        p.color = QColor(QStringLiteral("#7b2cbf"));
        p.brightnessPercent = 80;
        p.speed = 50;
        auto q = Profile::fromJson(p.id, p.toJson());
        QVERIFY(q);
        QCOMPARE(q->name, p.name);
        QCOMPARE(q->group, p.group);
        QCOMPARE(q->ledState(), p.ledState());
    }

    void qmkHueConversion()
    {
        QCOMPARE(ColorConv::toQmkHue(QColor(Qt::red)), 0);
        QCOMPARE(ColorConv::toQmkHue(QColor(Qt::green)), 85); // 120 deg
        QCOMPARE(ColorConv::toQmkHue(QColor(Qt::blue)), 171); // 240 deg
        QCOMPARE(ColorConv::toQmkHue(QColor(128, 128, 128)), 0);
        const QColor back = ColorConv::fromQmk(171, 255);
        QVERIFY(qAbs(back.hsvHue() - 240) <= 1);
    }

    void shippedDefaultsAreValid()
    {
        QTemporaryDir dir;
        ProfileManager pm(dir.path());
        QVERIFY(pm.ensureDefaults());
        QVERIFY(!pm.ensureDefaults()); // only once
        pm.reload();
        QVERIFY2(pm.loadErrors().isEmpty(), qPrintable(pm.loadErrors().join(QLatin1Char('\n'))));
        QCOMPARE(pm.profiles().size(), 23);
        const QStringList ffxiv = {QStringLiteral("ffxiv-arr"), QStringLiteral("ffxiv-heavensward"), QStringLiteral("ffxiv-stormblood"),
                                   QStringLiteral("ffxiv-shadowbringers"), QStringLiteral("ffxiv-endwalker"), QStringLiteral("ffxiv-dawntrail")};
        for (const auto &id : ffxiv) {
            QVERIFY2(pm.contains(id), qPrintable(id));
            QCOMPARE(pm.profile(id)->group, QStringLiteral("Final Fantasy XIV"));
        }

        QFile g(dir.path() + QStringLiteral("/games.json"));
        QVERIFY(g.open(QIODevice::ReadOnly));
        QString err;
        auto cfg = GameConfig::fromJson(g.readAll(), &err);
        QVERIFY2(cfg, qPrintable(err));
        QVERIFY(pm.contains(cfg->fallbackProfile));
        for (const auto &r : cfg->games) {
            QVERIFY2(pm.contains(r.profile), qPrintable(r.id + QStringLiteral(" -> ") + r.profile));
        }
        QVERIFY(cfg->find(QStringLiteral("ffxiv"))->steamAppIds.contains(39210));
        QVERIFY(cfg->find(QStringLiteral("wow-forever"))->processNames.contains(QStringLiteral("Wow.exe")));
    }

    void upgradesOnlyUntouchedDefaults()
    {
        QTemporaryDir dir;
        ProfileManager pm(dir.path());
        QDir().mkpath(pm.profilesDir());
        // A previous release's untouched default (byte-identical r1 file) and a user-edited one.
        const QByteArray r1Control = "{\n    \"name\": \"Control\",\n    \"group\": \"Games\",\n    \"description\": \"Oldest House red (black = unlit keys are not possible with one color).\",\n    \"effect\": \"Solid Color\",\n    \"color\": \"#d0021b\",\n    \"brightness\": 90,\n    \"speed\": 128\n}\n";
        auto write = [&](const QString &id, const QByteArray &data) {
            QFile f(pm.profilesDir() + QLatin1Char('/') + id + QStringLiteral(".json"));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(data);
        };
        write(QStringLiteral("control"), r1Control);
        write(QStringLiteral("bg3"), "{\"name\":\"My BG3\",\"effect\":\"Solid Color\",\"color\":\"#00ff00\"}");
        // (all other shipped profiles are "deleted" in this legacy install)
        QVERIFY(!pm.ensureDefaults()); // not a first run
        pm.reload();
        QVERIFY2(pm.profile(QStringLiteral("control"))->isGradient(), "untouched r1 default must be upgraded");
        QCOMPARE(pm.profile(QStringLiteral("bg3"))->name, QStringLiteral("My BG3")); // edited: kept
        QVERIFY(!pm.contains(QStringLiteral("hades"))); // deleted: not resurrected

        // Second run: nothing changes; the upgraded file is now tracked in .shipped.json
        QVERIFY(QFile::exists(pm.profilesDir() + QStringLiteral("/.shipped.json")));
        pm.ensureDefaults();
        pm.reload();
        QCOMPARE(pm.profile(QStringLiteral("bg3"))->name, QStringLiteral("My BG3"));

        // A newly shipped default (never recorded) gets installed
        QFile shipped(pm.profilesDir() + QStringLiteral("/.shipped.json"));
        QVERIFY(shipped.open(QIODevice::ReadOnly));
        QJsonObject o = QJsonDocument::fromJson(shipped.readAll()).object();
        shipped.close();
        QVERIFY(!pm.contains(QStringLiteral("hades"))); // still not resurrected on the second run
        // simulate a release that newly ships hades: never recorded, so it gets installed
        o.remove(QStringLiteral("hades"));
        QVERIFY(shipped.open(QIODevice::WriteOnly));
        shipped.write(QJsonDocument(o).toJson());
        shipped.close();
        pm.ensureDefaults();
        pm.reload();
        QVERIFY(pm.contains(QStringLiteral("hades")));

        // Explicit reset takes the shipped gradient
        QVERIFY(pm.resetToDefault(QStringLiteral("bg3")));
        QVERIFY(pm.profile(QStringLiteral("bg3"))->isGradient());
        QVERIFY(!pm.resetToDefault(QStringLiteral("my-own")));
        QVERIFY(ProfileManager::shippedIds().contains(QStringLiteral("ffxiv-dawntrail")));
    }

    void managerSaveRemoveAndErrors()
    {
        QTemporaryDir dir;
        ProfileManager pm(dir.path());
        QDir().mkpath(pm.profilesDir());
        QFile bad(pm.profilesDir() + QStringLiteral("/broken.json"));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("{ nope");
        bad.close();
        pm.reload();
        QCOMPARE(pm.loadErrors().size(), 1);
        QVERIFY(pm.loadErrors().first().startsWith(QStringLiteral("broken.json")));

        QSignalSpy spy(&pm, &ProfileManager::profilesChanged);
        Profile p;
        p.id = pm.uniqueId(QStringLiteral("My Game!"));
        QCOMPARE(p.id, QStringLiteral("my-game"));
        QVERIFY(pm.save(p));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(pm.uniqueId(QStringLiteral("My Game!")), QStringLiteral("my-game-2"));
        QCOMPARE(pm.uniqueId(QStringLiteral("keyboard")), QStringLiteral("keyboard-2")); // reserved
        QVERIFY(QFile::exists(pm.profilesDir() + QStringLiteral("/my-game.json")));
        QVERIFY(pm.remove(p.id));
        QVERIFY(!pm.contains(p.id));
    }
};

QTEST_GUILESS_MAIN(TestProfile)
#include "test_profile.moc"
