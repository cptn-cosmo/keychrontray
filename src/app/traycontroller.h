#pragma once

#include "appsettings.h"
#include "batteryalerts.h"
#include "batteryprovider.h"
#include "devicemonitor.h"
#include "gamedetector.h"
#include "keyboardbackend.h"
#include "lightingcontroller.h"
#include "profilemanager.h"
#include "steamlibrary.h"

#include <QObject>
#include <QPointer>

class KStatusNotifierItem;
class QMenu;
class SettingsDialog;

// Owns all modules and the tray icon. Wiring:
//   DeviceMonitor --hotplug--> KeyboardBackend::rescan --connected--> LightingController
//   GameDetector --game--> LightingController --activeChanged--> tray menu/tooltip
//   BatteryProvider --status--> tray icon/tooltip + BatteryAlerts --> KNotification
class TrayController : public QObject
{
    Q_OBJECT
public:
    explicit TrayController(bool mock, QObject *parent = nullptr);
    ~TrayController() override;

    KeyboardBackend *backend() const { return m_backend; }
    ProfileManager *profiles() const { return m_profiles; }
    LightingController *lighting() const { return m_lighting; }
    GameDetector *detector() const { return m_detector; }
    DeviceMonitor *devices() const { return m_devices; }
    BatteryProvider *battery() const { return m_battery; }
    SteamLibrary *steam() { return &m_steam; }
    AppSettings *settings() { return &m_settings; }

    void setAutoSwitch(bool on);
    void applySettings(); // re-read AppSettings into modules
    QString statusText() const;

public Q_SLOTS:
    void showSettings();
    void shutdown(); // restore lighting; called on aboutToQuit

private Q_SLOTS:
    void updateTray();
    void rebuildMenu();
    void onBattery(const BatteryStatus &s);
    void onPrepareForSleep(bool sleeping);

private:
    QString batteryText() const;
    QString iconName() const;
    void confirmSaveToKeyboard();

    AppSettings m_settings;
    SteamLibrary m_steam;
    KeyboardBackend *m_backend;
    DeviceMonitor *m_devices;
    BatteryProvider *m_battery;
    ProfileManager *m_profiles;
    LightingController *m_lighting;
    GameDetector *m_detector;
    BatteryAlerts m_alerts;
    KStatusNotifierItem *m_sni;
    QMenu *m_menu;
    QPointer<SettingsDialog> m_settingsDialog;
    bool m_shutDown = false;
};
