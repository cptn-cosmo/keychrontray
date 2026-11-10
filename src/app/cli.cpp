#include "cli.h"
#include "autostart.h"
#include "dbusinterface.h"
#include "gamedetector.h"
#include "hidkeyboardbackend.h"
#include "profilemanager.h"
#include "steamlibrary.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QTextStream>

namespace {
QTextStream &out()
{
    static QTextStream s(stdout);
    return s;
}
QTextStream &err()
{
    static QTextStream s(stderr);
    return s;
}

const char *kUsage = R"(Usage: keychrontray [command]

Without a command, starts the tray application.

Commands (talk to the running tray app over D-Bus):
  set-profile <id>        Switch lighting to a profile (stays until a game starts/exits)
  restore-default         Return to the default profile / auto-switching
  active-profile          Print the active profile id
  list-profiles           List profile ids, groups and names
  auto-switch on|off      Enable or disable game auto-switching
  status                  Show connection, battery and lighting status
  save-to-keyboard --yes  Write the current lighting to the keyboard's EEPROM (flash wear!)
  settings                Open the settings window
  reload                  Re-read profiles and games.json
  quit                    Quit the tray app (restores the original lighting)

Commands that work without the tray app:
  autostart on|off|status Start the tray app automatically at login (XDG autostart entry)
  add-game <steam-appid> [--name NAME] [--profile ID]
                          Add a game rule (and a new profile copied from the Steam fallback,
                          unless --profile is given)
  list-steam-games [N]    Show locally known Steam games by playtime (default 30)

Options:
  --mock                  Use the mock keyboard backend (no hardware)
  -h, --help              Show this help
)";

QDBusInterface *iface()
{
    static QDBusInterface *i = nullptr;
    if (!i) {
        i = new QDBusInterface(DBusService, DBusPath, DBusInterfaceName, QDBusConnection::sessionBus());
    }
    return i;
}

bool running()
{
    return QDBusConnection::sessionBus().interface()->isServiceRegistered(DBusService);
}

int needRunning()
{
    err() << "keychrontray is not running. Start it first (keychrontray &).\n";
    return 2;
}

QString optionValue(const QStringList &args, const QString &name)
{
    const qsizetype i = args.indexOf(name);
    return (i >= 0 && i + 1 < args.size()) ? args.at(i + 1) : QString();
}

int addGame(const QStringList &args)
{
    bool ok = false;
    const quint32 appId = args.value(1).toUInt(&ok);
    if (!ok || appId == 0) {
        err() << "add-game: expected a numeric Steam app id\n";
        return 1;
    }
    ProfileManager pm;
    pm.ensureDefaults();
    pm.reload();
    GameDetector det;
    if (!det.loadError().isEmpty()) {
        err() << "games.json: " << det.loadError() << "\n";
        return 1;
    }
    GameConfig cfg = det.config();
    for (const auto &r : cfg.games) {
        if (r.steamAppIds.contains(appId)) {
            err() << "app " << appId << " is already mapped by game '" << r.id << "' -> profile '" << r.profile << "'\n";
            return 1;
        }
    }
    QString name = optionValue(args, QStringLiteral("--name"));
    if (name.isEmpty()) {
        name = SteamLibrary().nameFor(appId);
    }
    if (name.isEmpty()) {
        name = QStringLiteral("Steam app %1").arg(appId);
    }
    QString profileId = optionValue(args, QStringLiteral("--profile"));
    if (!profileId.isEmpty()) {
        if (!pm.contains(profileId)) {
            err() << "unknown profile '" << profileId << "'\n";
            return 1;
        }
    } else {
        Profile p = pm.profile(cfg.fallbackProfile).value_or(Profile{});
        p.id = pm.uniqueId(name);
        p.name = name;
        p.group = QStringLiteral("Games");
        p.description.clear();
        QString e;
        if (!pm.save(p, &e)) {
            err() << "cannot save profile: " << e << "\n";
            return 1;
        }
        profileId = p.id;
        out() << "created profile " << pm.profilesDir() << "/" << p.id << ".json (edit its colors there or in Settings)\n";
    }
    GameRule r;
    r.id = profileId;
    for (int i = 2; cfg.find(r.id); ++i) {
        r.id = profileId + QLatin1Char('-') + QString::number(i);
    }
    r.name = name;
    r.steamAppIds = {appId};
    r.profile = profileId;
    cfg.games.append(r);
    QString e;
    if (!det.saveConfig(cfg, &e)) {
        err() << "cannot save games.json: " << e << "\n";
        return 1;
    }
    out() << "mapped " << name << " (" << appId << ") -> " << profileId << "\n";
    if (running()) {
        iface()->call(QStringLiteral("Reload"));
    }
    return 0;
}

int listSteam(const QStringList &args)
{
    const int limit = args.value(1, QStringLiteral("30")).toInt();
    SteamLibrary lib;
    if (!lib.isValid()) {
        err() << "no Steam installation found\n";
        return 1;
    }
    GameDetector det;
    QHash<quint32, QString> mapped;
    for (const auto &r : det.config().games) {
        for (auto id : r.steamAppIds) {
            mapped.insert(id, r.profile);
        }
    }
    int n = 0;
    for (const SteamApp &a : lib.knownApps()) {
        if (limit > 0 && n++ >= limit) {
            break;
        }
        out() << QStringLiteral("%1  %2 h  %3%4%5\n")
                     .arg(a.appId, 8)
                     .arg(a.playtimeMinutes / 60.0, 7, 'f', 1)
                     .arg(a.installed ? QStringLiteral("[installed] ") : QString(), a.name,
                          mapped.contains(a.appId) ? QStringLiteral("  -> ") + mapped.value(a.appId) : QString());
    }
    return 0;
}
}

std::optional<int> runCli(const QStringList &argsIn)
{
    QStringList args = argsIn.mid(1);
    args.removeAll(QStringLiteral("--mock"));
    if (args.isEmpty()) {
        return std::nullopt;
    }
    const QString cmd = args.first();
    if (cmd == QLatin1String("-h") || cmd == QLatin1String("--help") || cmd == QLatin1String("help")) {
        out() << kUsage;
        return 0;
    }
    if (cmd == QLatin1String("add-game")) {
        return addGame(args);
    }
    if (cmd == QLatin1String("list-steam-games")) {
        return listSteam(args);
    }
    if (cmd == QLatin1String("autostart")) {
        const QString v = args.value(1, QStringLiteral("status"));
        if (v == QLatin1String("status")) {
            out() << (Autostart::isEnabled() ? "enabled (" + Autostart::path() + ")\n" : QStringLiteral("disabled\n"));
            return 0;
        }
        if (v != QLatin1String("on") && v != QLatin1String("off")) {
            err() << "autostart: expected on, off or status\n";
            return 1;
        }
        QString e;
        if (!Autostart::setEnabled(v == QLatin1String("on"), &e)) {
            err() << "autostart: " << e << "\n";
            return 1;
        }
        out() << "autostart " << (v == QLatin1String("on") ? "enabled" : "disabled") << "\n";
        return 0;
    }

    static const QStringList remote = {QStringLiteral("set-profile"), QStringLiteral("restore-default"), QStringLiteral("active-profile"),
                                       QStringLiteral("list-profiles"), QStringLiteral("auto-switch"), QStringLiteral("status"),
                                       QStringLiteral("save-to-keyboard"), QStringLiteral("settings"), QStringLiteral("reload"),
                                       QStringLiteral("quit")};
    if (!remote.contains(cmd)) {
        err() << "unknown command '" << cmd << "'\n\n" << kUsage;
        return 1;
    }

    if (!running()) {
        if (cmd == QLatin1String("list-profiles")) {
            ProfileManager pm;
            pm.ensureDefaults();
            pm.reload();
            for (const Profile &p : pm.profiles()) {
                out() << p.id << "\t" << p.group << "\t" << p.displayName() << "\n";
            }
            return 0;
        }
        if (cmd == QLatin1String("set-profile") && args.size() > 1) {
            // One-shot: apply directly without the tray (no automatic restore).
            ProfileManager pm;
            pm.ensureDefaults();
            pm.reload();
            const auto p = pm.profile(args.at(1));
            if (!p) {
                err() << "unknown profile '" << args.at(1) << "'\n";
                return 1;
            }
            HidKeyboardBackend kb;
            kb.rescan();
            if (!kb.isConnected()) {
                err() << "keyboard not reachable (lighting needs the USB cable)\n";
                return 3;
            }
            if (!kb.apply(p->ledState())) {
                err() << "applying the profile failed\n";
                return 3;
            }
            out() << "applied " << p->id << " directly (tray app not running, nothing will restore it)\n";
            return 0;
        }
        return needRunning();
    }

    QDBusInterface *i = iface();
    if (cmd == QLatin1String("set-profile")) {
        if (args.size() < 2) {
            err() << "set-profile: missing profile id\n";
            return 1;
        }
        QDBusReply<bool> r = i->call(QStringLiteral("SetProfile"), args.at(1));
        if (!r.isValid() || !r.value()) {
            err() << "unknown profile '" << args.at(1) << "' (see: keychrontray list-profiles)\n";
            return 1;
        }
        return 0;
    }
    if (cmd == QLatin1String("restore-default")) {
        i->call(QStringLiteral("RestoreDefault"));
        return 0;
    }
    if (cmd == QLatin1String("active-profile")) {
        out() << QDBusReply<QString>(i->call(QStringLiteral("ActiveProfile"))).value() << "\n";
        return 0;
    }
    if (cmd == QLatin1String("list-profiles")) {
        for (const QString &l : QDBusReply<QStringList>(i->call(QStringLiteral("ListProfiles"))).value()) {
            out() << l << "\n";
        }
        return 0;
    }
    if (cmd == QLatin1String("auto-switch")) {
        const QString v = args.value(1);
        if (v != QLatin1String("on") && v != QLatin1String("off")) {
            err() << "auto-switch: expected on or off\n";
            return 1;
        }
        i->call(QStringLiteral("SetAutoSwitch"), v == QLatin1String("on"));
        return 0;
    }
    if (cmd == QLatin1String("status")) {
        out() << QDBusReply<QString>(i->call(QStringLiteral("Status"))).value() << "\n";
        return 0;
    }
    if (cmd == QLatin1String("save-to-keyboard")) {
        if (!args.contains(QStringLiteral("--yes"))) {
            err() << "This writes to the keyboard's EEPROM (flash wear). Re-run with --yes to confirm.\n";
            return 1;
        }
        const bool ok = QDBusReply<bool>(i->call(QStringLiteral("SaveToKeyboard"))).value();
        out() << (ok ? "saved\n" : "save failed\n");
        return ok ? 0 : 3;
    }
    if (cmd == QLatin1String("settings")) {
        i->call(QStringLiteral("ShowSettings"));
        return 0;
    }
    if (cmd == QLatin1String("reload")) {
        i->call(QStringLiteral("Reload"));
        return 0;
    }
    if (cmd == QLatin1String("quit")) {
        i->call(QStringLiteral("Quit"));
        return 0;
    }
    return 1;
}
