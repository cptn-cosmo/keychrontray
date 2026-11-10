#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <optional>

// One entry of ~/.config/keychrontray/games.json -> "games".
struct GameRule {
    QString id;
    QString name;
    QList<quint32> steamAppIds;
    QStringList processNames; // case-insensitive, wildcards allowed: "Wow*.exe"
    QString profile;
    int priority = 0; // higher wins when several games run at once
};

struct GameConfig {
    QList<GameRule> games;
    QString fallbackProfile; // for Steam games without a rule; empty = no switch
    QSet<quint32> ignoredSteamAppIds; // e.g. Proton / Steam Linux Runtime tool ids

    static std::optional<GameConfig> fromJson(const QByteArray &json, QString *error = nullptr);
    QByteArray toJson() const;
    GameRule *find(const QString &id);
};

struct RunningProcess {
    int pid = 0;
    QString comm; // /proc/<pid>/comm (max 15 chars)
    QString name; // basename of argv[0], "/" or "\" separated (wine paths)
    quint32 steamAppId = 0;
    qint64 startTime = 0; // clock ticks since boot (/proc/<pid>/stat field 22)
};

struct GameMatch {
    QString ruleId; // empty for the Steam fallback
    QString profile;
    QString label; // game name, or "Steam app <id>"
    quint32 steamAppId = 0;
    bool fallback = false;
    int priority = 0;
    qint64 startTime = 0;

    bool operator==(const GameMatch &o) const { return ruleId == o.ruleId && profile == o.profile && steamAppId == o.steamAppId && fallback == o.fallback; }
};

// Pure matching logic (unit-tested).
//
// Priority when several games are running:
//   1. a configured rule beats the generic Steam fallback
//   2. higher rule "priority" wins
//   3. otherwise the most recently started game wins
//   4. ties broken by rule id (deterministic)
// Non-Steam shortcuts (app id >= 2^31, e.g. a Battle.net launcher added to Steam) never use the fallback.
namespace GameMatcher {
inline bool isNonSteamShortcut(quint32 appId)
{
    return appId >= 0x80000000u;
}
bool processNameMatches(const QString &pattern, const RunningProcess &p);
std::optional<GameMatch> match(const GameConfig &config, const QList<RunningProcess> &processes);
}
