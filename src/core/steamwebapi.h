#pragma once

#include "steamlibrary.h"

#include <QNetworkAccessManager>
#include <QObject>

// Optional: full owned-games list via the Steam Web API (IPlayerService/GetOwnedGames).
// The API key comes from the config file and is never logged.
class SteamWebApi : public QObject
{
    Q_OBJECT
public:
    explicit SteamWebApi(QObject *parent = nullptr);

    // steamId may be a 64-bit SteamID or a vanity name (resolved via ISteamUser/ResolveVanityURL).
    void fetchOwnedGames(const QString &apiKey, const QString &steamId);

    static QList<SteamApp> parseOwnedGames(const QByteArray &json, QString *error = nullptr);

Q_SIGNALS:
    void ownedGamesReady(const QList<SteamApp> &apps);
    void failed(const QString &message);

private:
    void requestOwned(const QString &apiKey, const QString &steamId64);
    QNetworkAccessManager m_nam;
};
