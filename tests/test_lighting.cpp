#include "lightingcontroller.h"
#include "mockkeyboardbackend.h"
#include "profilemanager.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {
Profile mk(const QString &id, const char *color, uint8_t effect = 1)
{
    Profile p;
    p.id = id;
    p.name = id;
    p.color = QColor(QString::fromLatin1(color));
    p.effect = effect;
    return p;
}
}

class TestLighting : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    struct Rig {
        MockKeyboardBackend kb;
        ProfileManager pm;
        LightingController lc;
        Rig(const QString &dir)
            : pm(dir)
            , lc(&kb, &pm, dir + QStringLiteral("/state.json"))
        {
            pm.save(mk(QStringLiteral("red"), "#ff0000"));
            pm.save(mk(QStringLiteral("blue"), "#0000ff"));
            pm.save(mk(QStringLiteral("green"), "#00ff00", 2));
        }
    };

private Q_SLOTS:
    void init()
    {
        QFile::remove(m_dir.filePath(QStringLiteral("state.json")));
    }

    void capturesOriginalAndRestoresOnExit()
    {
        Rig r(m_dir.path());
        const LedState orig = r.kb.ram();
        r.lc.onConnectedChanged(true);
        QCOMPARE(r.lc.originalState().value(), orig);
        QVERIFY(r.lc.setManualProfile(QStringLiteral("red")));
        QCOMPARE(r.kb.ram().hue, 0);
        QCOMPARE(r.kb.ram().saturation, 255);
        QVERIFY(r.kb.ram() != orig);
        r.lc.restoreOriginal();
        QCOMPARE(r.kb.ram(), orig);
        QCOMPARE(r.kb.eepromWrites(), 0); // never touches EEPROM implicitly
    }

    void precedenceManualGameDefault()
    {
        Rig r(m_dir.path());
        r.lc.onConnectedChanged(true);
        r.lc.setDefaultProfile(QStringLiteral("blue"));
        QCOMPARE(r.lc.activeProfileId(), QStringLiteral("blue"));
        QCOMPARE(r.lc.activeReason(), QStringLiteral("default"));

        r.lc.setGameProfile(QStringLiteral("green"), QStringLiteral("Some Game"));
        QCOMPARE(r.lc.activeProfileId(), QStringLiteral("green"));
        QCOMPARE(r.lc.activeReason(), QStringLiteral("game: Some Game"));
        QCOMPARE(r.kb.ram().effect, 2);

        QVERIFY(r.lc.setManualProfile(QStringLiteral("red")));
        QCOMPARE(r.lc.activeProfileId(), QStringLiteral("red"));
        // Same game still running: manual stays
        r.lc.setGameProfile(QStringLiteral("green"), QStringLiteral("Some Game"));
        QCOMPARE(r.lc.activeProfileId(), QStringLiteral("red"));
        // Game exits: back to default
        r.lc.setGameProfile(QString(), QString());
        QCOMPARE(r.lc.activeProfileId(), QStringLiteral("blue"));

        QVERIFY(r.lc.setManualProfile(QStringLiteral("red")));
        r.lc.restoreDefault();
        QCOMPARE(r.lc.activeProfileId(), QStringLiteral("blue"));
        QVERIFY(!r.lc.setManualProfile(QStringLiteral("missing")));
    }

    void keyboardPseudoProfileIsOriginal()
    {
        Rig r(m_dir.path());
        const LedState orig = r.kb.ram();
        r.lc.onConnectedChanged(true);
        r.lc.setManualProfile(QStringLiteral("red"));
        QVERIFY(r.lc.setManualProfile(LightingController::KeyboardProfileId));
        QCOMPARE(r.kb.ram(), orig);
    }

    void avoidsRedundantWritesAndReappliesOnReconnect()
    {
        Rig r(m_dir.path());
        r.lc.onConnectedChanged(true);
        r.lc.setManualProfile(QStringLiteral("red"));
        const int n = r.kb.applyCount();
        r.lc.setManualProfile(QStringLiteral("red"));
        r.lc.reapply();
        QCOMPARE(r.kb.applyCount(), n);

        // Disconnected: nothing applied, no crash
        r.kb.setConnected(false);
        r.lc.setManualProfile(QStringLiteral("blue"));
        QCOMPARE(r.kb.applyCount(), n);
        // Reconnect: the active profile is pushed again
        r.kb.setConnected(true);
        QCOMPARE(r.kb.applyCount(), n + 1);
        QCOMPARE(r.kb.ram().hue, 171);
    }

    void previewOverridesAndEnds()
    {
        Rig r(m_dir.path());
        r.lc.onConnectedChanged(true);
        r.lc.setManualProfile(QStringLiteral("red"));
        LedState s{4, 10, 20, 30, 40};
        r.lc.preview(s);
        QCOMPARE(r.kb.ram(), s);
        QCOMPARE(r.lc.activeReason(), QStringLiteral("preview"));
        r.lc.endPreview();
        QCOMPARE(r.kb.ram().hue, 0);
    }

    void editedProfileIsReapplied()
    {
        Rig r(m_dir.path());
        r.lc.onConnectedChanged(true);
        r.lc.setManualProfile(QStringLiteral("red"));
        Profile p = *r.pm.profile(QStringLiteral("red"));
        p.brightnessPercent = 10;
        r.pm.save(p);
        QCOMPARE(r.kb.ram().brightness, 26);
    }

    void saveToKeyboardOnlyOnRequest()
    {
        Rig r(m_dir.path());
        r.lc.onConnectedChanged(true);
        r.lc.setManualProfile(QStringLiteral("green"));
        QCOMPARE(r.kb.eepromWrites(), 0);
        QVERIFY(r.lc.saveToKeyboard());
        QCOMPARE(r.kb.eepromWrites(), 1);
        QCOMPARE(r.kb.eeprom().effect, 2);
        // The saved state is the new baseline
        QCOMPARE(r.lc.originalState()->effect, 2);
    }

    void crashRecoveryUsesPersistedOriginal()
    {
        LedState orig;
        {
            Rig r(m_dir.path());
            orig = r.kb.ram();
            r.lc.onConnectedChanged(true);
            r.lc.setManualProfile(QStringLiteral("red"));
            // simulated crash: no restoreOriginal()
        }
        QFile f(m_dir.filePath(QStringLiteral("state.json")));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("dirty")).toBool());
        f.close();

        Rig r2(m_dir.path());
        r2.kb.apply(mk(QStringLiteral("red"), "#ff0000").ledState()); // keyboard still shows the game colors
        r2.lc.onConnectedChanged(true);
        QCOMPARE(r2.lc.originalState().value(), orig);
    }

    void applyFailureIsReported()
    {
        Rig r(m_dir.path());
        r.lc.onConnectedChanged(true);
        r.kb.setFailWrites(true);
        QSignalSpy spy(&r.lc, &LightingController::applyFailed);
        r.lc.setManualProfile(QStringLiteral("blue"));
        QCOMPARE(spy.count(), 1);
    }
};

QTEST_GUILESS_MAIN(TestLighting)
#include "test_lighting.moc"
