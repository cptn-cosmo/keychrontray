#include "appsettings.h"

#include <KConfigGroup>

AppSettings::AppSettings(KSharedConfigPtr config)
    : m_config(std::move(config))
{
}

#define GENERAL KConfigGroup(m_config, QStringLiteral("General"))
#define STEAM KConfigGroup(m_config, QStringLiteral("Steam"))

QString AppSettings::defaultProfile() const
{
    return GENERAL.readEntry("DefaultProfile", QStringLiteral("keyboard"));
}
void AppSettings::setDefaultProfile(const QString &id)
{
    GENERAL.writeEntry("DefaultProfile", id);
}
bool AppSettings::autoSwitch() const
{
    return GENERAL.readEntry("AutoSwitch", true);
}
void AppSettings::setAutoSwitch(bool on)
{
    GENERAL.writeEntry("AutoSwitch", on);
}
int AppSettings::scanIntervalSeconds() const
{
    return qBound(1, GENERAL.readEntry("ScanIntervalSeconds", 3), 60);
}
void AppSettings::setScanIntervalSeconds(int s)
{
    GENERAL.writeEntry("ScanIntervalSeconds", s);
}
QList<int> AppSettings::batteryThresholds() const
{
    return GENERAL.readEntry("BatteryThresholds", QList<int>{20, 10});
}
void AppSettings::setBatteryThresholds(const QList<int> &t)
{
    GENERAL.writeEntry("BatteryThresholds", t);
}
QString AppSettings::steamApiKey() const
{
    return STEAM.readEntry("ApiKey", QString());
}
void AppSettings::setSteamApiKey(const QString &k)
{
    STEAM.writeEntry("ApiKey", k);
}
QString AppSettings::steamId() const
{
    return STEAM.readEntry("SteamId", QString());
}
void AppSettings::setSteamId(const QString &id)
{
    STEAM.writeEntry("SteamId", id);
}
void AppSettings::sync()
{
    m_config->sync();
}
