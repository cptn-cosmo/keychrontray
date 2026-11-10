#pragma once

#include <KSharedConfig>
#include <QList>
#include <QString>

// ~/.config/keychrontrayrc
//
// [General]
// DefaultProfile=keyboard        # profile used when no game runs; "keyboard" = lighting found at startup
// AutoSwitch=true
// ScanIntervalSeconds=3
// BatteryThresholds=20,10
//
// [Steam]
// ApiKey=                        # optional, for the full owned-games list
// SteamId=                       # SteamID64 or vanity name
class AppSettings
{
public:
    explicit AppSettings(KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("keychrontrayrc")));

    QString defaultProfile() const;
    void setDefaultProfile(const QString &id);
    bool autoSwitch() const;
    void setAutoSwitch(bool on);
    int scanIntervalSeconds() const;
    void setScanIntervalSeconds(int s);
    QList<int> batteryThresholds() const;
    void setBatteryThresholds(const QList<int> &t);
    QString steamApiKey() const;
    void setSteamApiKey(const QString &k);
    QString steamId() const;
    void setSteamId(const QString &id);

    void sync();

private:
    KSharedConfigPtr m_config;
};
