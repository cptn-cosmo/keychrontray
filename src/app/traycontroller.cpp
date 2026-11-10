#include "traycontroller.h"
#include "autostart.h"
#include "batteryicon.h"
#include "hidkeyboardbackend.h"
#include "logging.h"
#include "mockkeyboardbackend.h"
#include "settingsdialog.h"

#include <KNotification>
#include <KStatusNotifierItem>
#include <QActionGroup>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QTimer>

TrayController::TrayController(bool mock, QObject *parent)
    : QObject(parent)
{
    if (mock) {
        m_backend = new MockKeyboardBackend(this);
        qCInfo(lcApp) << "using mock keyboard backend";
    } else {
        m_backend = new HidKeyboardBackend(this);
    }
    m_devices = new DeviceMonitor(this);
    m_battery = new BatteryProvider(this);
    m_profiles = new ProfileManager(QString(), this);
    m_profiles->ensureDefaults();
    m_profiles->reload();
    m_lighting = new LightingController(m_backend, m_profiles, QString(), this);
    m_detector = new GameDetector(QString(), this);
    m_detector->setNameLookup([this](quint32 id) {
        return m_steam.nameFor(id);
    });

    m_sni = new KStatusNotifierItem(QStringLiteral("keychrontray"), this);
    m_sni->setCategory(KStatusNotifierItem::Hardware);
    m_sni->setTitle(QStringLiteral("Keychron Tray"));
    m_sni->setStatus(KStatusNotifierItem::Active);
    m_sni->setStandardActionsEnabled(false);
    m_menu = new QMenu();
    m_sni->setContextMenu(m_menu);
    connect(m_sni, &KStatusNotifierItem::activateRequested, this, [this](bool, const QPoint &) {
        showSettings();
    });

    connect(m_devices, &DeviceMonitor::devicesChanged, m_backend, &KeyboardBackend::rescan);
    connect(m_devices, &DeviceMonitor::devicesChanged, m_battery, &BatteryProvider::refresh);
    connect(m_devices, &DeviceMonitor::connectionChanged, this, &TrayController::updateTray);
    connect(m_backend, &KeyboardBackend::connectedChanged, this, &TrayController::updateTray);
    connect(m_battery, &BatteryProvider::statusChanged, this, &TrayController::onBattery);
    connect(m_profiles, &ProfileManager::profilesChanged, this, &TrayController::rebuildMenu);
    connect(m_lighting, &LightingController::activeChanged, this, &TrayController::rebuildMenu);
    connect(m_lighting, &LightingController::activeChanged, this, &TrayController::updateTray);
    connect(m_lighting, &LightingController::applyFailed, this, [](const QString &msg) {
        KNotification::event(QStringLiteral("lightingError"), QStringLiteral("Keychron lighting"), msg, QStringLiteral("input-keyboard"),
                             KNotification::CloseOnTimeout, QStringLiteral("keychrontray"));
    });
    connect(m_detector, &GameDetector::gameChanged, this, [this](const std::optional<GameMatch> &m) {
        m_lighting->setGameProfile(m ? m->profile : QString(), m ? m->label : QString());
        updateTray();
    });

    // Sleep/resume: devices may re-enumerate and the keyboard may reset its RAM state.
    QDBusConnection::systemBus().connect(QStringLiteral("org.freedesktop.login1"), QStringLiteral("/org/freedesktop/login1"),
                                         QStringLiteral("org.freedesktop.login1.Manager"), QStringLiteral("PrepareForSleep"), this,
                                         SLOT(onPrepareForSleep(bool)));

    applySettings();
    m_backend->rescan(); // emits connectedChanged -> LightingController captures + applies
    rebuildMenu();
    updateTray();
}

TrayController::~TrayController()
{
    shutdown();
    // m_menu is owned (and deleted) by KStatusNotifierItem.
}

void TrayController::applySettings()
{
    m_lighting->setDefaultProfile(m_settings.defaultProfile());
    m_alerts.setThresholds(m_settings.batteryThresholds());
    m_detector->setInterval(m_settings.scanIntervalSeconds() * 1000);
    m_detector->setEnabled(m_settings.autoSwitch());
    rebuildMenu();
    updateTray();
}

void TrayController::setAutoSwitch(bool on)
{
    m_settings.setAutoSwitch(on);
    m_settings.sync();
    m_detector->setEnabled(on);
    rebuildMenu();
    updateTray();
}

void TrayController::shutdown()
{
    if (m_shutDown) {
        return;
    }
    m_shutDown = true;
    m_detector->setEnabled(false);
    m_lighting->restoreOriginal();
    // Child objects are destroyed after our destructor body; don't let their signals reach us then.
    for (QObject *o : {static_cast<QObject *>(m_backend), static_cast<QObject *>(m_devices), static_cast<QObject *>(m_battery),
                       static_cast<QObject *>(m_profiles), static_cast<QObject *>(m_lighting), static_cast<QObject *>(m_detector)}) {
        disconnect(o, nullptr, this, nullptr);
    }
}

void TrayController::onPrepareForSleep(bool sleeping)
{
    if (sleeping) {
        qCInfo(lcApp) << "system going to sleep";
        return;
    }
    qCInfo(lcApp) << "resumed; rescanning devices";
    QTimer::singleShot(2000, this, [this] {
        m_devices->refresh();
        m_backend->rescan();
        m_battery->refresh();
        m_lighting->reapply(true);
        updateTray();
    });
}

void TrayController::onBattery(const BatteryStatus &s)
{
    const int t = m_alerts.update(s.available ? s.percent : -1, s.charging);
    if (t > 0) {
        KNotification::event(QStringLiteral("lowBattery"), QStringLiteral("Keyboard battery low"),
                             QStringLiteral("Your Keychron keyboard is at %1% (below %2%).").arg(s.percent).arg(t),
                             BatteryIcon::name(s.percent, false,
                                               [](const QString &n) {
                                                   return QIcon::hasThemeIcon(n);
                                               }),
                             KNotification::Persistent, QStringLiteral("keychrontray"));
    }
    updateTray();
}

QString TrayController::batteryText() const
{
    const BatteryStatus s = m_battery->status();
    if (s.available) {
        return QStringLiteral("%1%%2").arg(s.percent).arg(s.charging ? QStringLiteral(", charging") : QString());
    }
    const ConnectionType c = m_devices->connection();
    if (c == ConnectionType::Disconnected) {
        return QStringLiteral("disconnected");
    }
    return QStringLiteral("unavailable (%1)").arg(connectionLabel(c));
}

QString TrayController::iconName() const
{
    const BatteryStatus s = m_battery->status();
    return BatteryIcon::name(s.available ? s.percent : -1, s.charging, [](const QString &n) {
        return QIcon::hasThemeIcon(n);
    });
}

QString TrayController::statusText() const
{
    const KeyboardInfo info = m_backend->info();
    QStringList lines;
    lines << QStringLiteral("Connection: %1").arg(connectionLabel(m_devices->connection()));
    lines << QStringLiteral("Battery: %1").arg(batteryText());
    if (m_backend->isConnected()) {
        const QString id = m_lighting->activeProfileId();
        const auto p = m_profiles->profile(id);
        lines << QStringLiteral("Profile: %1 (%2)").arg(p ? p->displayName() : QStringLiteral("Keyboard default"), m_lighting->activeReason());
    } else {
        lines << QStringLiteral("Lighting: unavailable (needs the USB cable; the 2.4 GHz receiver does not forward lighting commands)");
        lines << QStringLiteral("Selected profile: %1 (%2)").arg(m_lighting->activeProfileId(), m_lighting->activeReason());
    }
    lines << QStringLiteral("Auto-switch: %1").arg(m_detector->isEnabled() ? QStringLiteral("on") : QStringLiteral("off"));
    if (m_detector->current()) {
        lines << QStringLiteral("Detected game: %1").arg(m_detector->current()->label);
    }
    return lines.join(QLatin1Char('\n'));
}

void TrayController::updateTray()
{
    const KeyboardInfo info = m_backend->info();
    const QString title = info.product.isEmpty() ? QStringLiteral("Keychron keyboard") : info.product;
    const QString icon = iconName();
    m_sni->setIconByName(icon);
    const bool disconnected = m_devices->connection() == ConnectionType::Disconnected && !m_backend->isConnected();
    m_sni->setOverlayIconByName(disconnected ? QStringLiteral("emblem-unavailable") : QString());
    m_sni->setToolTip(icon, title, statusText().toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
}

void TrayController::rebuildMenu()
{
    m_menu->clear();
    const QString active = m_lighting->activeProfileId();

    auto *header = m_menu->addAction(QStringLiteral("Battery: %1").arg(batteryText()));
    header->setEnabled(false);
    if (!m_backend->isConnected()) {
        auto *a = m_menu->addAction(QStringLiteral("Lighting needs the USB cable"));
        a->setEnabled(false);
    }
    m_menu->addSeparator();

    auto *group = new QActionGroup(m_menu);
    auto addProfile = [&](QMenu *menu, const QString &id, const QString &label) {
        QAction *a = menu->addAction(label);
        a->setCheckable(true);
        a->setChecked(id == active);
        group->addAction(a);
        connect(a, &QAction::triggered, this, [this, id] {
            m_lighting->setManualProfile(id);
        });
        return a;
    };
    addProfile(m_menu, LightingController::KeyboardProfileId, QStringLiteral("Keyboard default"));
    QMap<QString, QMenu *> groups;
    for (const Profile &p : m_profiles->profiles()) {
        QMenu *target = m_menu;
        if (!p.group.isEmpty()) {
            if (!groups.contains(p.group)) {
                groups.insert(p.group, m_menu->addMenu(p.group));
            }
            target = groups.value(p.group);
            if (p.id == active) {
                target->setTitle(QStringLiteral("%1  ●").arg(p.group));
            }
        }
        addProfile(target, p.id, p.displayName());
    }
    if (!m_profiles->loadErrors().isEmpty()) {
        auto *a = m_menu->addAction(QIcon::fromTheme(QStringLiteral("dialog-warning")), QStringLiteral("%1 profile file(s) have errors…").arg(m_profiles->loadErrors().size()));
        connect(a, &QAction::triggered, this, &TrayController::showSettings);
    }

    m_menu->addSeparator();
    QAction *autoAct = m_menu->addAction(QStringLiteral("Auto-switch for games"));
    autoAct->setCheckable(true);
    autoAct->setChecked(m_detector->isEnabled());
    connect(autoAct, &QAction::toggled, this, &TrayController::setAutoSwitch);

    QAction *restore = m_menu->addAction(QIcon::fromTheme(QStringLiteral("edit-undo")), QStringLiteral("Restore default"));
    connect(restore, &QAction::triggered, m_lighting, &LightingController::restoreDefault);

    QAction *save = m_menu->addAction(QIcon::fromTheme(QStringLiteral("document-save")), QStringLiteral("Save current lighting to keyboard…"));
    save->setEnabled(m_backend->isConnected());
    connect(save, &QAction::triggered, this, &TrayController::confirmSaveToKeyboard);

    QAction *autostart = m_menu->addAction(QStringLiteral("Start automatically at login"));
    autostart->setCheckable(true);
    autostart->setChecked(Autostart::isEnabled());
    connect(autostart, &QAction::toggled, this, [](bool on) {
        QString err;
        if (!Autostart::setEnabled(on, &err)) {
            QMessageBox::warning(nullptr, QStringLiteral("Autostart"), QStringLiteral("Could not change autostart: %1").arg(err));
        }
    });

    m_menu->addSeparator();
    QAction *settings = m_menu->addAction(QIcon::fromTheme(QStringLiteral("configure")), QStringLiteral("Open settings…"));
    connect(settings, &QAction::triggered, this, &TrayController::showSettings);
    QAction *quit = m_menu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")), QStringLiteral("Quit"));
    connect(quit, &QAction::triggered, qApp, &QCoreApplication::quit);
}

void TrayController::confirmSaveToKeyboard()
{
    const auto r = QMessageBox::question(nullptr, QStringLiteral("Save to keyboard"),
                                         QStringLiteral("Write the current lighting into the keyboard's memory (EEPROM)?\n\n"
                                                        "It becomes the keyboard's own default, also on other computers. "
                                                        "Each save is a flash write, so this is never done automatically."));
    if (r == QMessageBox::Yes && !m_lighting->saveToKeyboard()) {
        QMessageBox::warning(nullptr, QStringLiteral("Save to keyboard"), QStringLiteral("Saving failed. Is the keyboard connected by USB cable?"));
    }
}

void TrayController::showSettings()
{
    if (!m_settingsDialog) {
        m_settingsDialog = new SettingsDialog(this);
        m_settingsDialog->setAttribute(Qt::WA_DeleteOnClose);
    }
    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}
