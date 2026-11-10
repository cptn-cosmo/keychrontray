#include "mockkeyboardbackend.h"
#include "settingsdialog.h"
#include "traycontroller.h"

#include <QCheckBox>
#include <QComboBox>
#include <QListWidget>
#include <QSlider>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>

class TestSettingsUi : public QObject
{
    Q_OBJECT
    QTemporaryDir m_home;

private Q_SLOTS:
    void initTestCase()
    {
        // Never touch the real user config.
        qputenv("XDG_CONFIG_HOME", QFile::encodeName(m_home.filePath(QStringLiteral("config"))));
        qputenv("XDG_STATE_HOME", QFile::encodeName(m_home.filePath(QStringLiteral("state"))));
        QStandardPaths::setTestModeEnabled(false);
    }

    void buildsAndPreviews()
    {
        TrayController tray(/*mock=*/true);
        auto *kb = qobject_cast<MockKeyboardBackend *>(tray.backend());
        QVERIFY(kb);
        QVERIFY(tray.profiles()->profilesDir().startsWith(m_home.path()));
        QCOMPARE(tray.profiles()->profiles().size(), 23);
        QVERIFY(tray.lighting()->originalState()); // captured on (mock) connect

        SettingsDialog dlg(&tray);
        dlg.resize(980, 640);
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));

        auto *list = dlg.findChild<QListWidget *>();
        QVERIFY(list);
        QCOMPARE(list->count(), 23);
        auto *games = dlg.findChild<QTableWidget *>();
        QVERIFY(games);
        QCOMPARE(games->rowCount(), 16);

        // Selecting a profile previews it live on the (mock) keyboard
        for (int i = 0; i < list->count(); ++i) {
            if (list->item(i)->data(Qt::UserRole).toString() == QLatin1String("ffxiv-endwalker")) {
                list->setCurrentRow(i);
            }
        }
        QCOMPARE(kb->ram(), tray.profiles()->profile(QStringLiteral("ffxiv-endwalker"))->ledState());
        // Editing brightness previews immediately, without touching EEPROM
        auto sliders = dlg.findChildren<QSlider *>();
        QVERIFY(!sliders.isEmpty());
        sliders.first()->setValue(10);
        QCOMPARE(kb->ram().brightness, 26);
        QCOMPARE(kb->eepromWrites(), 0);

        const QString shots = qEnvironmentVariable("KEYCHRONTRAY_SCREENSHOT_DIR");
        auto *tabs = dlg.findChild<QTabWidget *>();
        for (int i = 0; i < tabs->count(); ++i) {
            tabs->setCurrentIndex(i);
            QTest::qWait(50);
            if (!shots.isEmpty()) {
                dlg.grab().save(QStringLiteral("%1/settings-%2.png").arg(shots).arg(i));
            }
        }
        // Discard the unsaved edit and close: preview ends, previous lighting returns
        dlg.findChild<QSlider *>()->setValue(tray.profiles()->profile(QStringLiteral("ffxiv-endwalker"))->brightnessPercent);
        tabs->setCurrentIndex(0);
        // (closing would ask "Save changes?" for the edit above - correct, but modal; shut down directly)
        tray.shutdown();
        QCOMPARE(kb->ram(), tray.lighting()->originalState().value()); // restored on exit
        QCOMPARE(kb->eepromWrites(), 0);
    }
};

QTEST_MAIN(TestSettingsUi)
#include "test_settingsui.moc"
