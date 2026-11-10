#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

struct SteamApp {
    quint32 appId = 0;
    QString name;
    bool installed = false;
    int playtimeMinutes = 0;
};

// Reads the local Steam installation. Nothing here touches the network.
class SteamLibrary
{
public:
    // root defaults to the first existing of ~/.local/share/Steam, ~/.steam/steam, Flatpak Steam.
    explicit SteamLibrary(const QString &root = QString());

    QString root() const { return m_root; }
    bool isValid() const { return !m_root.isEmpty(); }

    QStringList libraryFolders() const;
    // Installed games from appmanifest_*.acf.
    QList<SteamApp> installedApps() const;
    // Playtime (minutes) per app from userdata/*/config/localconfig.vdf (max across local accounts).
    QHash<quint32, int> playtime() const;
    // App names from the binary appcache/appinfo.vdf (covers games Steam has seen, installed or not).
    QHash<quint32, QString> appInfoNames() const;

    // Installed + played apps merged, sorted by playtime descending.
    QList<SteamApp> knownApps() const;

    // Name of one app: manifest first (cheap), then appinfo cache.
    QString nameFor(quint32 appId) const;

    static QHash<quint32, QString> parseAppInfo(const QByteArray &data);

private:
    QString m_root;
    mutable QHash<quint32, QString> m_manifestNames;
    mutable bool m_manifestsLoaded = false;
    mutable QHash<quint32, QString> m_appInfo;
    mutable bool m_appInfoLoaded = false;
};
