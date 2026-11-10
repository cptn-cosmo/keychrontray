#include "gradient.h"
#include "gradientanimator.h"
#include "lightingcontroller.h"
#include "mockkeyboardbackend.h"
#include "profilemanager.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {
Gradient grad(QList<const char *> colors, Gradient::Motion m = Gradient::Motion::Static, double cycle = 10)
{
    Gradient g;
    for (auto c : colors) {
        g.stops.append(QColor(QString::fromLatin1(c)));
    }
    g.motion = m;
    g.cycleSeconds = cycle;
    return g;
}
QList<double> positions(int n)
{
    QList<double> xs;
    for (int i = 0; i < n; ++i) {
        xs.append(double(i) / (n - 1));
    }
    return xs;
}
}

class TestGradient : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;

private Q_SLOTS:
    void staticEndpointsAndMiddle()
    {
        const Gradient g = grad({"#ff0000", "#0000ff"});
        QCOMPARE(g.sample(0.0), (HueSat{0, 255}));
        QCOMPARE(g.sample(1.0), (HueSat{171, 255}));
        // red(0) -> blue(171): shorter arc goes backwards through magenta (hue 213)
        QCOMPARE(g.sample(0.5).h, uint8_t(213));
        QCOMPARE(g.sample(-1.0), g.sample(0.0)); // clamped
        QCOMPARE(g.sample(2.0), g.sample(1.0));
    }

    void threeStopsEvenlySpaced()
    {
        const Gradient g = grad({"#ff0000", "#00ff00", "#0000ff"});
        QCOMPARE(g.sample(0.5), (HueSat{85, 255}));
        QCOMPARE(g.sample(0.25).h, uint8_t(43)); // halfway red->green
    }

    void whiteStopOnlyChangesSaturation()
    {
        const Gradient g = grad({"#ff0000", "#ffffff"});
        const HueSat mid = g.sample(0.5);
        QCOMPARE(mid.h, uint8_t(0)); // keeps red's hue, no detour through other hues
        QVERIFY(qAbs(int(mid.s) - 128) <= 1);
        QCOMPARE(g.sample(1.0).s, uint8_t(0));
    }

    void scrollWrapsSeamlesslyAndMovesRight()
    {
        const Gradient g = grad({"#ff0000", "#00ff00", "#0000ff"}, Gradient::Motion::Scroll, 10);
        for (double phase : {0.0, 0.13, 0.5, 0.97}) {
            QCOMPARE(g.sample(0.0, phase), g.sample(1.0, phase)); // cyclic: edges match
            // Moving right: the color at x now is at x + d after the phase advanced by d (±1 rounding)...
            const HueSat now = g.sample(0.30, phase), later = g.sample(0.40, phase + 0.1), wrong = g.sample(0.20, phase + 0.1);
            QVERIFY2(qAbs(int(later.h) - int(now.h)) <= 1 && later.s == now.s, qPrintable(QStringLiteral("phase %1").arg(phase)));
            // ...and not to the left.
            QVERIFY(qAbs(int(wrong.h) - int(now.h)) > 10);
        }
        QCOMPARE(g.phaseAt(0), 0.0);
        QCOMPARE(g.phaseAt(5000), 0.5);
        QCOMPARE(g.phaseAt(12500), 0.25);
        QCOMPARE(grad({"#ff0000", "#0000ff"}).phaseAt(5000), 0.0); // static never moves
    }

    void renderMatchesSample()
    {
        const Gradient g = grad({"#19b3a6", "#f2c14e"});
        const auto xs = positions(21);
        const auto frame = g.render(xs, 0.0);
        QCOMPARE(frame.size(), 21);
        QCOMPARE(frame.first(), g.sample(0.0));
        QCOMPARE(frame.last(), g.sample(1.0));
    }

    void profileParsing()
    {
        QString err;
        auto p = Profile::fromJson(QStringLiteral("g"),
                                   "{\"gradient\":{\"colors\":[\"#ff0000\",\"#0000ff\"],\"motion\":\"scroll\",\"cycleSeconds\":8},\"brightness\":50}", &err);
        QVERIFY2(p, qPrintable(err));
        QVERIFY(p->isGradient());
        QCOMPARE(p->gradient->stops.size(), 2);
        QCOMPARE(p->gradient->motion, Gradient::Motion::Scroll);
        QCOMPARE(p->gradient->cycleSeconds, 8.0);
        QCOMPARE(p->ledState().effect, Effects::PerKeyRgb);
        QCOMPARE(p->ledState().brightness, 128);
        auto q = Profile::fromJson(QStringLiteral("g"), p->toJson());
        QVERIFY(q);
        QCOMPARE(q->gradient, p->gradient);

        const QList<QByteArray> bad = {
            "{\"gradient\":{\"colors\":[\"#ff0000\"]}}",
            "{\"gradient\":{\"colors\":[\"#ff0000\",\"nope\"]}}",
            "{\"gradient\":{\"colors\":[\"#ff0000\",\"#00ff00\"],\"motion\":\"spin\"}}",
            "{\"gradient\":{\"colors\":[\"#ff0000\",\"#00ff00\"],\"cycleSeconds\":0}}",
            "{\"gradient\":[1,2]}",
        };
        for (const auto &b : bad) {
            QVERIFY2(!Profile::fromJson(QStringLiteral("g"), b, &err), b.constData());
        }
    }

    void shippedThemesAreVisibleGradients()
    {
        QTemporaryDir dir;
        ProfileManager pm(dir.path());
        pm.ensureDefaults();
        pm.reload();
        int gradients = 0;
        for (const Profile &p : pm.profiles()) {
            if (p.id == QLatin1String("desk")) {
                QVERIFY(!p.isGradient()); // neutral work profile stays solid white
                continue;
            }
            QVERIFY2(p.isGradient(), qPrintable(p.id));
            QVERIFY2(p.gradient->stops.size() >= 2, qPrintable(p.id));
            // At least two stops must differ in what the keyboard can show (hue/saturation).
            QSet<QPair<int, int>> distinct;
            for (const QColor &c : p.gradient->stops) {
                const HueSat hs = HsvBlend::toHueSat(c);
                distinct.insert({hs.h, hs.s});
            }
            QVERIFY2(distinct.size() >= 2, qPrintable(p.id));
            ++gradients;
        }
        QCOMPARE(gradients, 22);
    }

    void controllerAppliesGradientAndRestores()
    {
        MockKeyboardBackend kb;
        ProfileManager pm(m_dir.path());
        Profile p;
        p.id = QStringLiteral("grad");
        p.gradient = grad({"#ff0000", "#0000ff"});
        p.brightnessPercent = 80;
        pm.save(p);
        LightingController lc(&kb, &pm, m_dir.filePath(QStringLiteral("state.json")));
        const LedState origBase = kb.ram();
        const QList<HueSat> origTable = kb.perKey();
        lc.onConnectedChanged(true);
        QCOMPARE(lc.originalPerKey().value(), origTable);

        QVERIFY(lc.setManualProfile(QStringLiteral("grad")));
        QCOMPARE(kb.ram().effect, Effects::PerKeyRgb);
        QCOMPARE(kb.perKey(), p.gradient->render(kb.ledPositions()));
        QCOMPARE(kb.perKey().first(), (HueSat{0, 255}));
        QCOMPARE(kb.perKey()[20], (HueSat{171, 255})); // rightmost column
        QVERIFY(!lc.animator()->isRunning()); // static

        lc.restoreOriginal();
        QCOMPARE(kb.ram(), origBase);
        QCOMPARE(kb.perKey(), origTable);
        QCOMPARE(kb.eepromWrites(), 0);
    }

    void scrollingAnimatesThenStops()
    {
        MockKeyboardBackend kb;
        ProfileManager pm(m_dir.path());
        Profile p;
        p.id = QStringLiteral("scroll");
        p.gradient = grad({"#ff0000", "#00ff00", "#0000ff"}, Gradient::Motion::Scroll, 2);
        pm.save(p);
        LightingController lc(&kb, &pm, m_dir.filePath(QStringLiteral("state2.json")));
        lc.animator()->setFrameIntervalMs(20);
        lc.onConnectedChanged(true);
        lc.setManualProfile(QStringLiteral("scroll"));
        QVERIFY(lc.animator()->isRunning());
        const QList<HueSat> first = kb.perKey();
        QTRY_VERIFY_WITH_TIMEOUT(lc.animator()->framesRendered() >= 5, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(kb.perKey() != first, 3000);

        // Switching to a solid profile stops the animation
        Profile solid;
        solid.id = QStringLiteral("solid");
        pm.save(solid);
        lc.setManualProfile(QStringLiteral("solid"));
        QVERIFY(!lc.animator()->isRunning());
        const int writes = kb.perKeyWrites();
        QTest::qWait(100);
        QCOMPARE(kb.perKeyWrites(), writes);
        QCOMPARE(kb.ram().effect, uint8_t(1));
        QCOMPARE(kb.eepromWrites(), 0);
    }

    void animatorSkipsUnchangedChunks()
    {
        MockKeyboardBackend kb;
        GradientAnimator a(&kb);
        a.setFrameIntervalMs(10);
        // One color repeated: every frame is identical -> nothing re-sent after the first frame.
        Gradient g = grad({"#ff0000", "#ff0000"}, Gradient::Motion::Scroll, 2);
        QVERIFY(a.showFrame(g));
        const int afterFirst = kb.perKeyWrites();
        QCOMPARE(afterFirst, 12); // 104 LEDs / 9 per packet
        a.start(g);
        QTRY_VERIFY_WITH_TIMEOUT(a.framesRendered() >= 5, 2000);
        QCOMPARE(kb.perKeyWrites(), afterFirst);
        a.stop();
    }

    void noPerKeySupportFallsBackToSolid()
    {
        MockKeyboardBackend kb;
        kb.setPerKeySupported(false);
        ProfileManager pm(m_dir.path());
        Profile p;
        p.id = QStringLiteral("fallback");
        p.gradient = grad({"#00ff00", "#0000ff"}, Gradient::Motion::Scroll);
        pm.save(p);
        LightingController lc(&kb, &pm, m_dir.filePath(QStringLiteral("state3.json")));
        lc.onConnectedChanged(true);
        lc.setManualProfile(QStringLiteral("fallback"));
        QCOMPARE(kb.ram().effect, uint8_t(1));
        QCOMPARE(kb.ram().hue, uint8_t(85)); // first stop, green
        QVERIFY(!lc.animator()->isRunning());
        QCOMPARE(kb.perKeyWrites(), 0);
    }

    void crashRecoveryKeepsOriginalTable()
    {
        const QString state = m_dir.filePath(QStringLiteral("state4.json"));
        QList<HueSat> origTable;
        {
            MockKeyboardBackend kb;
            ProfileManager pm(m_dir.path());
            Profile p;
            p.id = QStringLiteral("crash");
            p.gradient = grad({"#ff0000", "#0000ff"});
            pm.save(p);
            LightingController lc(&kb, &pm, state);
            origTable = kb.perKey();
            lc.onConnectedChanged(true);
            lc.setManualProfile(QStringLiteral("crash"));
            // crash: no restoreOriginal()
        }
        MockKeyboardBackend kb2;
        ProfileManager pm2(m_dir.path());
        pm2.reload();
        kb2.writePerKey(0, QList<HueSat>(9, HueSat{171, 255})); // keyboard still shows our gradient
        LightingController lc2(&kb2, &pm2, state);
        lc2.onConnectedChanged(true);
        QCOMPARE(lc2.originalPerKey().value(), origTable);
        lc2.restoreOriginal();
        QCOMPARE(kb2.perKey(), origTable);
    }
};

QTEST_GUILESS_MAIN(TestGradient)
#include "test_gradient.moc"
