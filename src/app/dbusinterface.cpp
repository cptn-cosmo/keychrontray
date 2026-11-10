#include "dbusinterface.h"
#include "traycontroller.h"

#include <QCoreApplication>
#include <QTimer>

DBusInterface::DBusInterface(TrayController *tray)
    : QDBusAbstractAdaptor(tray)
    , m_tray(tray)
{
}

bool DBusInterface::SetProfile(const QString &id)
{
    return m_tray->lighting()->setManualProfile(id);
}

void DBusInterface::RestoreDefault()
{
    m_tray->lighting()->restoreDefault();
}

QStringList DBusInterface::ListProfiles()
{
    QStringList out;
    out << QStringLiteral("keyboard\t\tKeyboard default (lighting at startup)");
    for (const Profile &p : m_tray->profiles()->profiles()) {
        out << QStringLiteral("%1\t%2\t%3").arg(p.id, p.group, p.displayName());
    }
    return out;
}

QString DBusInterface::ActiveProfile()
{
    return m_tray->lighting()->activeProfileId();
}

void DBusInterface::SetAutoSwitch(bool on)
{
    m_tray->setAutoSwitch(on);
}

bool DBusInterface::AutoSwitch()
{
    return m_tray->detector()->isEnabled();
}

QString DBusInterface::Status()
{
    return m_tray->statusText();
}

bool DBusInterface::SaveToKeyboard()
{
    return m_tray->lighting()->saveToKeyboard();
}

void DBusInterface::Reload()
{
    m_tray->profiles()->reload();
    m_tray->detector()->reloadConfig();
}

void DBusInterface::ShowSettings()
{
    m_tray->showSettings();
}

void DBusInterface::Quit()
{
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}
