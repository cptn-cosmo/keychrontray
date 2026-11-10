#include "gamerules.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

std::optional<GameConfig> GameConfig::fromJson(const QByteArray &json, QString *error)
{
    auto fail = [error](const QString &msg) -> std::optional<GameConfig> {
        if (error) {
            *error = msg;
        }
        return std::nullopt;
    };
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError) {
        return fail(QStringLiteral("JSON error at offset %1: %2").arg(pe.offset).arg(pe.errorString()));
    }
    const QJsonObject o = doc.object();
    GameConfig c;
    c.fallbackProfile = o.value(QLatin1String("fallbackProfile")).toString();
    for (const auto &v : o.value(QLatin1String("ignoredSteamAppIds")).toArray()) {
        c.ignoredSteamAppIds.insert(quint32(v.toInteger()));
    }
    QSet<QString> ids;
    for (const auto &gv : o.value(QLatin1String("games")).toArray()) {
        const QJsonObject g = gv.toObject();
        GameRule r;
        r.id = g.value(QLatin1String("id")).toString();
        if (r.id.isEmpty()) {
            return fail(QStringLiteral("game entry without \"id\""));
        }
        if (ids.contains(r.id)) {
            return fail(QStringLiteral("duplicate game id '%1'").arg(r.id));
        }
        ids.insert(r.id);
        r.name = g.value(QLatin1String("name")).toString(r.id);
        r.profile = g.value(QLatin1String("profile")).toString();
        r.priority = g.value(QLatin1String("priority")).toInt(0);
        for (const auto &a : g.value(QLatin1String("steamAppIds")).toArray()) {
            const qint64 id = a.toInteger(-1);
            if (id <= 0 || id > 0xFFFFFFFFLL) {
                return fail(QStringLiteral("game '%1': invalid Steam app id").arg(r.id));
            }
            r.steamAppIds.append(quint32(id));
        }
        for (const auto &p : g.value(QLatin1String("processNames")).toArray()) {
            if (!p.toString().isEmpty()) {
                r.processNames.append(p.toString());
            }
        }
        c.games.append(r);
    }
    return c;
}

QByteArray GameConfig::toJson() const
{
    QJsonObject o;
    o.insert(QLatin1String("fallbackProfile"), fallbackProfile);
    QList<quint32> ignored(ignoredSteamAppIds.begin(), ignoredSteamAppIds.end());
    std::sort(ignored.begin(), ignored.end());
    QJsonArray ia;
    for (auto id : ignored) {
        ia.append(qint64(id));
    }
    o.insert(QLatin1String("ignoredSteamAppIds"), ia);
    QJsonArray games;
    for (const auto &r : this->games) {
        QJsonObject g;
        g.insert(QLatin1String("id"), r.id);
        g.insert(QLatin1String("name"), r.name);
        QJsonArray apps;
        for (auto a : r.steamAppIds) {
            apps.append(qint64(a));
        }
        g.insert(QLatin1String("steamAppIds"), apps);
        g.insert(QLatin1String("processNames"), QJsonArray::fromStringList(r.processNames));
        g.insert(QLatin1String("profile"), r.profile);
        g.insert(QLatin1String("priority"), r.priority);
        games.append(g);
    }
    o.insert(QLatin1String("games"), games);
    return QJsonDocument(o).toJson(QJsonDocument::Indented);
}

GameRule *GameConfig::find(const QString &id)
{
    for (auto &r : games) {
        if (r.id == id) {
            return &r;
        }
    }
    return nullptr;
}

bool GameMatcher::processNameMatches(const QString &pattern, const RunningProcess &p)
{
    // NonPathWildcardConversion: '*' also matches '\' and '/'; names are already basenames.
    const QRegularExpression anchored(QRegularExpression::wildcardToRegularExpression(pattern, QRegularExpression::NonPathWildcardConversion),
                                      QRegularExpression::CaseInsensitiveOption);
    if (anchored.match(p.name).hasMatch()) {
        return true;
    }
    // comm is truncated to 15 characters by the kernel; only compare if the pattern could fit.
    return !p.comm.isEmpty() && anchored.match(p.comm).hasMatch();
}

std::optional<GameMatch> GameMatcher::match(const GameConfig &config, const QList<RunningProcess> &processes)
{
    std::optional<GameMatch> best;
    auto better = [](const GameMatch &a, const GameMatch &b) {
        if (a.fallback != b.fallback) {
            return !a.fallback;
        }
        if (a.priority != b.priority) {
            return a.priority > b.priority;
        }
        if (a.startTime != b.startTime) {
            return a.startTime > b.startTime;
        }
        return a.ruleId < b.ruleId;
    };
    auto consider = [&](const GameMatch &m) {
        if (!best || better(m, *best)) {
            best = m;
        }
    };

    for (const RunningProcess &p : processes) {
        bool matched = false;
        if (p.steamAppId != 0 && config.ignoredSteamAppIds.contains(p.steamAppId)) {
            continue;
        }
        for (const GameRule &r : config.games) {
            bool hit = p.steamAppId != 0 && r.steamAppIds.contains(p.steamAppId);
            for (int i = 0; !hit && i < r.processNames.size(); ++i) {
                hit = processNameMatches(r.processNames[i], p);
            }
            if (hit) {
                matched = true;
                consider(GameMatch{r.id, r.profile, r.name, p.steamAppId, false, r.priority, p.startTime});
            }
        }
        // Non-Steam shortcuts (launchers, emulators, ...) get ids >= 2^31; they only match explicit rules.
        if (!matched && p.steamAppId != 0 && !isNonSteamShortcut(p.steamAppId) && !config.fallbackProfile.isEmpty()) {
            consider(GameMatch{QString(), config.fallbackProfile, QStringLiteral("Steam app %1").arg(p.steamAppId), p.steamAppId, true, 0, p.startTime});
        }
    }
    return best;
}
