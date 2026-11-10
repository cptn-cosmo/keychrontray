#pragma once

#include <QDBusAbstractAdaptor>
#include <QStringList>

class TrayController;

inline const QString DBusService = QStringLiteral("io.github.keychrontray");
inline const QString DBusPath = QStringLiteral("/KeychronTray");
inline const QString DBusInterfaceName = QStringLiteral("io.github.keychrontray.Control");

// D-Bus API, e.g.
//   qdbus6 io.github.keychrontray /KeychronTray io.github.keychrontray.Control.SetProfile ffxiv-endwalker
class DBusInterface : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.keychrontray.Control")
public:
    explicit DBusInterface(TrayController *tray);

public Q_SLOTS:
    bool SetProfile(const QString &id);
    void RestoreDefault();
    QStringList ListProfiles(); // "id<TAB>group<TAB>name"
    QString ActiveProfile();
    void SetAutoSwitch(bool on);
    bool AutoSwitch();
    QString Status();
    bool SaveToKeyboard();
    void Reload();
    void ShowSettings();
    void Quit();

private:
    TrayController *m_tray;
};
