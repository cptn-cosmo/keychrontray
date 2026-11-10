#include "steamlibrary.h"
#include "steamwebapi.h"
#include "vdf.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

namespace {
void write(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}
void le32(QByteArray &b, quint32 v)
{
    char buf[4];
    qToLittleEndian(v, buf);
    b.append(buf, 4);
}
}

class TestSteam : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void vdfParse()
    {
        bool ok = false;
        const auto v = Vdf::parse(R"("libraryfolders"
{
    // comment
    "0"
    {
        "path"      "/home/u/.local/share/Steam"
        "apps" { "39210" "123" }
    }
    "1" { "path" "/games/Steam\\Lib" }
})",
                                  &ok);
        QVERIFY(ok);
        const auto lf = Vdf::child(v, QStringLiteral("LibraryFolders")).toMap();
        QCOMPARE(lf.size(), 2);
        QCOMPARE(lf.value(QStringLiteral("1")).toMap().value(QStringLiteral("path")).toString(), QStringLiteral("/games/Steam\\Lib"));
        QCOMPARE(Vdf::child(lf.value(QStringLiteral("0")).toMap(), QStringLiteral("apps")).toMap().value(QStringLiteral("39210")).toString(),
                 QStringLiteral("123"));

        Vdf::parse("\"a\" { \"b\" \"c\"", &ok);
        QVERIFY(!ok);
    }

    void libraryAndPlaytime()
    {
        QTemporaryDir dir;
        const QString root = dir.filePath(QStringLiteral("Steam"));
        const QString lib2 = dir.filePath(QStringLiteral("Lib2"));
        QDir().mkpath(lib2 + QStringLiteral("/steamapps"));
        write(root + QStringLiteral("/steamapps/libraryfolders.vdf"),
              QStringLiteral("\"libraryfolders\" { \"0\" { \"path\" \"%1\" } \"1\" { \"path\" \"%2\" } }").arg(root, lib2).toUtf8());
        write(root + QStringLiteral("/steamapps/appmanifest_39210.acf"), R"("AppState" { "appid" "39210" "name" "FINAL FANTASY XIV Online" })");
        write(lib2 + QStringLiteral("/steamapps/appmanifest_720620.acf"), R"("AppState" { "appid" "720620" "name" "TaleSpire" })");
        write(root + QStringLiteral("/userdata/1/config/localconfig.vdf"), R"("UserLocalConfigStore" { "Software" { "Valve" { "Steam" { "apps" {
            "39210" { "Playtime" "71766" }
            "720620" { "Playtime" "6930" }
            "999" { "Playtime" "5" }
        } } } } })");

        SteamLibrary s(root);
        QCOMPARE(s.libraryFolders().size(), 2);
        QCOMPARE(s.installedApps().size(), 2);
        QCOMPARE(s.playtime().value(39210), 71766);
        QCOMPARE(s.nameFor(720620), QStringLiteral("TaleSpire"));
        const auto known = s.knownApps(); // 999 has no name -> dropped
        QCOMPARE(known.size(), 2);
        QCOMPARE(known.first().appId, 39210u);
    }

    void appInfoV29()
    {
        // Header, one app entry with appinfo/common/name, terminator, string table.
        QByteArray d;
        le32(d, 0x07564429);
        le32(d, 1);
        const qsizetype strOffPos = d.size();
        d.append(8, '\0'); // string table offset placeholder

        QByteArray kv;
        kv.append('\0');
        le32(kv, 0); // "appinfo"
        kv.append('\0');
        le32(kv, 1); // "common"
        kv.append('\2');
        le32(kv, 3); // "type" int -> skipped
        le32(kv, 7);
        kv.append('\1');
        le32(kv, 2); // "name"
        kv.append("Control\0", 8);
        kv.append('\x08');
        kv.append('\x08');
        kv.append('\x08');

        le32(d, 870780);
        le32(d, quint32(60 + kv.size()));
        d.append(60, '\0');
        d.append(kv);
        le32(d, 0); // end

        const qint64 strOff = d.size();
        le32(d, 4);
        d.append("appinfo\0common\0name\0type\0", 25);
        qToLittleEndian(strOff, d.data() + strOffPos);

        const auto names = SteamLibrary::parseAppInfo(d);
        QCOMPARE(names.value(870780), QStringLiteral("Control"));
        QVERIFY(SteamLibrary::parseAppInfo(QByteArray(20, 'x')).isEmpty());
    }

    void webApiParse()
    {
        QString err;
        const auto apps = SteamWebApi::parseOwnedGames(
            R"({"response":{"game_count":2,"games":[{"appid":10,"name":"A","playtime_forever":5},{"appid":20,"name":"B","playtime_forever":50}]}})", &err);
        QCOMPARE(apps.size(), 2);
        QCOMPARE(apps.first().appId, 20u);
        QVERIFY(SteamWebApi::parseOwnedGames(R"({"response":{}})", &err).isEmpty());
        QVERIFY(err.contains(QStringLiteral("public")));
    }
};

QTEST_GUILESS_MAIN(TestSteam)
#include "test_steam.moc"
