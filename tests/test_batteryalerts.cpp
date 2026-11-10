#include "batteryalerts.h"
#include "batteryicon.h"

#include <QSet>

#include <QTest>

class TestBatteryAlerts : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void firesOncePerCrossing()
    {
        BatteryAlerts a({20, 10});
        QCOMPARE(a.update(50, false), -1);
        QCOMPARE(a.update(20, false), 20);
        QCOMPARE(a.update(19, false), -1);
        QCOMPARE(a.update(15, false), -1);
        QCOMPARE(a.update(10, false), 10);
        QCOMPARE(a.update(5, false), -1);
    }

    void bigDropReportsLowest()
    {
        BatteryAlerts a({20, 10});
        QCOMPARE(a.update(8, false), 10);
        QCOMPARE(a.update(8, false), -1);
    }

    void rearmsAfterChargingOrHysteresis()
    {
        BatteryAlerts a({20, 10}, 3);
        QCOMPARE(a.update(19, false), 20);
        QCOMPARE(a.update(21, false), -1); // within hysteresis, still armed-off
        QCOMPARE(a.update(19, false), -1);
        QCOMPARE(a.update(23, false), -1); // re-armed
        QCOMPARE(a.update(20, false), 20);
        QCOMPARE(a.update(20, true), -1); // charging re-arms, no alert
        QCOMPARE(a.update(19, false), 20);
    }

    void keyboardBatteryIcons()
    {
        // Names as installed by Breeze: input-keyboard-battery-000 .. -100, each with a -charging variant
        QSet<QString> theme;
        for (int l = 0; l <= 100; l += 10) {
            const QString n = QStringLiteral("input-keyboard-battery-%1").arg(l, 3, 10, QLatin1Char('0'));
            theme << n << n + QStringLiteral("-charging");
        }
        auto has = [&](const QString &n) {
            return theme.contains(n);
        };
        QCOMPARE(BatteryIcon::name(0, false, has), QStringLiteral("input-keyboard-battery-000"));
        QCOMPARE(BatteryIcon::name(4, false, has), QStringLiteral("input-keyboard-battery-000"));
        QCOMPARE(BatteryIcon::name(5, false, has), QStringLiteral("input-keyboard-battery-010"));
        QCOMPARE(BatteryIcon::name(57, true, has), QStringLiteral("input-keyboard-battery-060-charging"));
        QCOMPARE(BatteryIcon::name(100, true, has), QStringLiteral("input-keyboard-battery-100-charging"));
        QCOMPARE(BatteryIcon::name(-1, false, has), QStringLiteral("input-keyboard")); // unavailable

        // Older theme without keyboard icons: generic battery icons
        auto generic = [](const QString &n) {
            return n.startsWith(QLatin1String("battery-"));
        };
        QCOMPARE(BatteryIcon::name(20, false, generic), QStringLiteral("battery-020"));
        QCOMPARE(BatteryIcon::name(20, false, [](const QString &) {
                     return false;
                 }),
                 QStringLiteral("input-keyboard"));
    }

    void thresholdsSanitised()
    {
        BatteryAlerts a({10, 0, 20, 20, 150});
        QCOMPARE(a.thresholds(), (QList<int>{20, 10}));
        QCOMPARE(a.update(-1, false), -1); // unknown level
    }
};

QTEST_GUILESS_MAIN(TestBatteryAlerts)
#include "test_batteryalerts.moc"
