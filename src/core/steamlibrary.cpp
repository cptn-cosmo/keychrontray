#include "steamlibrary.h"
#include "logging.h"
#include "vdf.h"

#include <QDir>
#include <QFile>
#include <QtEndian>

namespace {
QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
}

SteamLibrary::SteamLibrary(const QString &root)
{
    if (!root.isEmpty()) {
        m_root = root;
        return;
    }
    const QString home = QDir::homePath();
    for (const QString &c : QStringList{home + QStringLiteral("/.local/share/Steam"), home + QStringLiteral("/.steam/steam"),
                             home + QStringLiteral("/.var/app/com.valvesoftware.Steam/.local/share/Steam")}) {
        if (QFile::exists(c + QStringLiteral("/steamapps"))) {
            m_root = QDir(c).canonicalPath();
            break;
        }
    }
}

QStringList SteamLibrary::libraryFolders() const
{
    QStringList out;
    if (m_root.isEmpty()) {
        return out;
    }
    out << m_root;
    const QVariantMap v = Vdf::parse(readFile(m_root + QStringLiteral("/steamapps/libraryfolders.vdf")));
    const QVariantMap folders = Vdf::child(v, QStringLiteral("libraryfolders")).toMap();
    for (const QVariant &f : folders) {
        const QString path = Vdf::child(f.toMap(), QStringLiteral("path")).toString();
        if (!path.isEmpty()) {
            const QString canon = QDir(path).canonicalPath();
            if (!canon.isEmpty() && !out.contains(canon)) {
                out << canon;
            }
        }
    }
    return out;
}

QList<SteamApp> SteamLibrary::installedApps() const
{
    QList<SteamApp> out;
    for (const QString &lib : libraryFolders()) {
        QDir dir(lib + QStringLiteral("/steamapps"));
        for (const QString &f : dir.entryList({QStringLiteral("appmanifest_*.acf")}, QDir::Files)) {
            const QVariantMap st = Vdf::child(Vdf::parse(readFile(dir.filePath(f))), QStringLiteral("AppState")).toMap();
            SteamApp a;
            a.appId = Vdf::child(st, QStringLiteral("appid")).toString().toUInt();
            a.name = Vdf::child(st, QStringLiteral("name")).toString();
            a.installed = true;
            if (a.appId) {
                out << a;
            }
        }
    }
    return out;
}

QHash<quint32, int> SteamLibrary::playtime() const
{
    QHash<quint32, int> out;
    if (m_root.isEmpty()) {
        return out;
    }
    QDir ud(m_root + QStringLiteral("/userdata"));
    for (const QString &user : ud.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QVariantMap lc = Vdf::parse(readFile(ud.filePath(user + QStringLiteral("/config/localconfig.vdf"))));
        QVariantMap apps = Vdf::child(lc, QStringLiteral("UserLocalConfigStore")).toMap();
        for (const char *k : {"Software", "Valve", "Steam", "apps"}) {
            apps = Vdf::child(apps, QString::fromLatin1(k)).toMap();
        }
        for (auto it = apps.constBegin(); it != apps.constEnd(); ++it) {
            const int pt = Vdf::child(it.value().toMap(), QStringLiteral("Playtime")).toString().toInt();
            const quint32 id = it.key().toUInt();
            if (id && pt > out.value(id)) {
                out.insert(id, pt);
            }
        }
    }
    return out;
}

QHash<quint32, QString> SteamLibrary::parseAppInfo(const QByteArray &d)
{
    QHash<quint32, QString> names;
    auto u32 = [&d](qsizetype off) {
        return qFromLittleEndian<quint32>(d.constData() + off);
    };
    if (d.size() < 16) {
        return names;
    }
    const quint32 magic = u32(0);
    const bool v29 = magic == 0x07564429;
    const bool v28 = magic == 0x07564428;
    if (!v29 && !v28) {
        qCWarning(lcGames) << "appinfo.vdf: unsupported version" << Qt::hex << magic;
        return names;
    }
    qsizetype off = 8;
    QList<QByteArray> strtab;
    if (v29) {
        const qint64 so = qFromLittleEndian<qint64>(d.constData() + 8);
        off = 16;
        if (so <= 0 || so + 4 > d.size()) {
            return names;
        }
        const quint32 n = u32(so);
        qsizetype pos = so + 4;
        strtab.reserve(n);
        for (quint32 k = 0; k < n && pos < d.size(); ++k) {
            const qsizetype e = d.indexOf('\0', pos);
            if (e < 0) {
                break;
            }
            strtab << d.mid(pos, e - pos);
            pos = e + 1;
        }
    }
    constexpr qsizetype HeaderSkip = 4 + 4 + 8 + 20 + 4 + 20; // infostate, lastupdated, picstoken, sha1, change, binsha1
    while (off + 8 <= d.size()) {
        const quint32 appid = u32(off);
        if (appid == 0) {
            break;
        }
        const quint32 size = u32(off + 4);
        const qsizetype start = off + 8 + HeaderSkip;
        const qsizetype end = off + 8 + size;
        off = end;
        if (end > d.size() || start >= end) {
            break;
        }
        // Walk binary KV looking for appinfo/common/name.
        qsizetype i = start;
        QList<QByteArray> stack;
        auto readKey = [&](QByteArray *k) -> bool {
            if (v29) {
                if (i + 4 > end) {
                    return false;
                }
                const quint32 idx = u32(i);
                i += 4;
                if (idx >= quint32(strtab.size())) {
                    return false;
                }
                *k = strtab[idx];
                return true;
            }
            const qsizetype e = d.indexOf('\0', i);
            if (e < 0 || e >= end) {
                return false;
            }
            *k = d.mid(i, e - i);
            i = e + 1;
            return true;
        };
        while (i < end) {
            const char t = d[i++];
            if (t == 8 || t == 11) {
                if (stack.isEmpty()) {
                    break;
                }
                stack.removeLast();
                continue;
            }
            QByteArray key;
            if (!readKey(&key)) {
                break;
            }
            if (t == 0) {
                stack << key;
            } else if (t == 1) {
                const qsizetype e = d.indexOf('\0', i);
                if (e < 0 || e >= end) {
                    break;
                }
                if (key == "name" && stack.size() == 2 && stack[0] == "appinfo" && stack[1] == "common") {
                    names.insert(appid, QString::fromUtf8(d.mid(i, e - i)));
                    break;
                }
                i = e + 1;
            } else if (t == 2 || t == 3 || t == 4 || t == 6) {
                i += 4;
            } else if (t == 7 || t == 10) {
                i += 8;
            } else {
                break; // unknown type: give up on this entry only
            }
        }
    }
    return names;
}

QHash<quint32, QString> SteamLibrary::appInfoNames() const
{
    if (!m_appInfoLoaded && !m_root.isEmpty()) {
        m_appInfo = parseAppInfo(readFile(m_root + QStringLiteral("/appcache/appinfo.vdf")));
        m_appInfoLoaded = true;
    }
    return m_appInfo;
}

QList<SteamApp> SteamLibrary::knownApps() const
{
    QHash<quint32, SteamApp> all;
    for (const SteamApp &a : installedApps()) {
        all.insert(a.appId, a);
    }
    const auto pt = playtime();
    const auto names = appInfoNames();
    for (auto it = pt.constBegin(); it != pt.constEnd(); ++it) {
        SteamApp &a = all[it.key()];
        a.appId = it.key();
        a.playtimeMinutes = it.value();
        if (a.name.isEmpty()) {
            a.name = names.value(it.key());
        }
    }
    QList<SteamApp> out = all.values();
    out.removeIf([](const SteamApp &a) {
        return a.name.isEmpty();
    });
    std::sort(out.begin(), out.end(), [](const SteamApp &a, const SteamApp &b) {
        if (a.playtimeMinutes != b.playtimeMinutes) {
            return a.playtimeMinutes > b.playtimeMinutes;
        }
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return out;
}

QString SteamLibrary::nameFor(quint32 appId) const
{
    if (!m_manifestsLoaded) {
        for (const SteamApp &a : installedApps()) {
            m_manifestNames.insert(a.appId, a.name);
        }
        m_manifestsLoaded = true;
    }
    const QString n = m_manifestNames.value(appId);
    return n.isEmpty() ? appInfoNames().value(appId) : n;
}
