#include "steamwebapi.h"
#include "logging.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QUrlQuery>

SteamWebApi::SteamWebApi(QObject *parent)
    : QObject(parent)
{
}

QList<SteamApp> SteamWebApi::parseOwnedGames(const QByteArray &json, QString *error)
{
    QList<SteamApp> out;
    const QJsonObject resp = QJsonDocument::fromJson(json).object().value(QLatin1String("response")).toObject();
    if (!resp.contains(QLatin1String("games"))) {
        if (error) {
            *error = QStringLiteral("no games in response (is the profile's game list public?)");
        }
        return out;
    }
    for (const auto &g : resp.value(QLatin1String("games")).toArray()) {
        const QJsonObject o = g.toObject();
        SteamApp a;
        a.appId = quint32(o.value(QLatin1String("appid")).toInteger());
        a.name = o.value(QLatin1String("name")).toString();
        a.playtimeMinutes = o.value(QLatin1String("playtime_forever")).toInt();
        if (a.appId) {
            out << a;
        }
    }
    std::sort(out.begin(), out.end(), [](const SteamApp &a, const SteamApp &b) {
        return a.playtimeMinutes > b.playtimeMinutes;
    });
    return out;
}

void SteamWebApi::fetchOwnedGames(const QString &apiKey, const QString &steamId)
{
    if (apiKey.isEmpty() || steamId.isEmpty()) {
        Q_EMIT failed(QStringLiteral("Steam API key and SteamID/vanity name are required"));
        return;
    }
    static const QRegularExpression numeric(QStringLiteral("^\\d{17}$"));
    if (numeric.match(steamId).hasMatch()) {
        requestOwned(apiKey, steamId);
        return;
    }
    QUrl url(QStringLiteral("https://api.steampowered.com/ISteamUser/ResolveVanityURL/v1/"));
    url.setQuery(QUrlQuery{{QStringLiteral("key"), apiKey}, {QStringLiteral("vanityurl"), steamId}});
    qCInfo(lcGames) << "resolving vanity name via Steam Web API";
    QNetworkReply *r = m_nam.get(QNetworkRequest(url));
    connect(r, &QNetworkReply::finished, this, [this, r, apiKey] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            Q_EMIT failed(QStringLiteral("ResolveVanityURL failed: HTTP %1").arg(r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()));
            return;
        }
        const QJsonObject resp = QJsonDocument::fromJson(r->readAll()).object().value(QLatin1String("response")).toObject();
        const QString id = resp.value(QLatin1String("steamid")).toString();
        if (id.isEmpty()) {
            Q_EMIT failed(QStringLiteral("Vanity name not found"));
            return;
        }
        requestOwned(apiKey, id);
    });
}

void SteamWebApi::requestOwned(const QString &apiKey, const QString &steamId64)
{
    QUrl url(QStringLiteral("https://api.steampowered.com/IPlayerService/GetOwnedGames/v1/"));
    url.setQuery(QUrlQuery{{QStringLiteral("key"), apiKey},
                           {QStringLiteral("steamid"), steamId64},
                           {QStringLiteral("include_appinfo"), QStringLiteral("1")},
                           {QStringLiteral("include_played_free_games"), QStringLiteral("1")}});
    qCInfo(lcGames) << "fetching owned games via Steam Web API"; // URL deliberately not logged (contains key)
    QNetworkReply *r = m_nam.get(QNetworkRequest(url));
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            Q_EMIT failed(QStringLiteral("GetOwnedGames failed: HTTP %1").arg(r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()));
            return;
        }
        QString err;
        const auto apps = parseOwnedGames(r->readAll(), &err);
        if (apps.isEmpty()) {
            Q_EMIT failed(err.isEmpty() ? QStringLiteral("no games returned") : err);
            return;
        }
        qCInfo(lcGames) << "Steam Web API returned" << apps.size() << "games";
        Q_EMIT ownedGamesReady(apps);
    });
}
