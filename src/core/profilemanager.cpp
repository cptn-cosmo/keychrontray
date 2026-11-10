#include "profilemanager.h"
#include "logging.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>

// Resources in a static library must be registered explicitly; must be called outside any namespace.
static void initDefaultsResource()
{
    Q_INIT_RESOURCE(defaults);
}

ProfileManager::ProfileManager(const QString &configDir, QObject *parent)
    : QObject(parent)
    , m_configDir(configDir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/keychrontray") : configDir)
{
    initDefaultsResource();
    m_reloadDebounce.setSingleShot(true);
    m_reloadDebounce.setInterval(300);
    connect(&m_reloadDebounce, &QTimer::timeout, this, &ProfileManager::reload);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_reloadDebounce, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_reloadDebounce, qOverload<>(&QTimer::start));
}

QString ProfileManager::profilesDir() const
{
    return m_configDir + QStringLiteral("/profiles");
}

bool ProfileManager::ensureDefaults()
{
    QDir dir(profilesDir());
    if (dir.exists()) {
        return false;
    }
    if (!dir.mkpath(QStringLiteral("."))) {
        qCWarning(lcProfiles) << "cannot create" << dir.path();
        return false;
    }
    QDirIterator it(QStringLiteral(":/defaults/profiles"), {QStringLiteral("*.json")});
    int n = 0;
    while (it.hasNext()) {
        const QString src = it.next();
        const QString dst = dir.filePath(QFileInfo(src).fileName());
        if (QFile::copy(src, dst)) {
            QFile::setPermissions(dst, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
            ++n;
        }
    }
    const QString games = m_configDir + QStringLiteral("/games.json");
    if (!QFile::exists(games) && QFile::copy(QStringLiteral(":/defaults/games.json"), games)) {
        QFile::setPermissions(games, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
    }
    qCInfo(lcProfiles) << "installed" << n << "default profiles into" << dir.path();
    return true;
}

void ProfileManager::reload()
{
    QMap<QString, Profile> loaded;
    QStringList errors;
    QDir dir(profilesDir());
    const auto files = dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QFileInfo &fi : files) {
        QFile f(fi.filePath());
        if (!f.open(QIODevice::ReadOnly)) {
            errors << QStringLiteral("%1: cannot read").arg(fi.fileName());
            continue;
        }
        QString err;
        auto p = Profile::fromJson(fi.completeBaseName(), f.readAll(), &err);
        if (!p) {
            errors << QStringLiteral("%1: %2").arg(fi.fileName(), err);
            continue;
        }
        loaded.insert(p->id, *p);
    }
    for (const auto &e : std::as_const(errors)) {
        qCWarning(lcProfiles) << "profile error:" << e;
    }
    m_profiles = loaded;
    m_errors = errors;

    if (dir.exists()) {
        if (!m_watcher.directories().contains(dir.path())) {
            m_watcher.addPath(dir.path());
        }
        const QStringList watched = m_watcher.files();
        if (!watched.isEmpty()) {
            m_watcher.removePaths(watched);
        }
        QStringList paths;
        for (const auto &fi : files) {
            paths << fi.filePath();
        }
        if (!paths.isEmpty()) {
            m_watcher.addPaths(paths);
        }
    }
    qCInfo(lcProfiles) << "loaded" << m_profiles.size() << "profiles";
    Q_EMIT profilesChanged();
}

QList<Profile> ProfileManager::profiles() const
{
    QList<Profile> list = m_profiles.values();
    std::sort(list.begin(), list.end(), [](const Profile &a, const Profile &b) {
        if (a.group != b.group) {
            if (a.group.isEmpty() != b.group.isEmpty()) {
                return a.group.isEmpty(); // ungrouped first
            }
            return a.group.localeAwareCompare(b.group) < 0;
        }
        return a.displayName().localeAwareCompare(b.displayName()) < 0;
    });
    return list;
}

std::optional<Profile> ProfileManager::profile(const QString &id) const
{
    auto it = m_profiles.constFind(id);
    if (it == m_profiles.constEnd()) {
        return std::nullopt;
    }
    return *it;
}

bool ProfileManager::save(const Profile &p, QString *error)
{
    if (!Profile::isValidId(p.id)) {
        if (error) {
            *error = QStringLiteral("invalid profile id");
        }
        return false;
    }
    QDir().mkpath(profilesDir());
    QSaveFile f(profilesDir() + QLatin1Char('/') + p.id + QStringLiteral(".json"));
    if (!f.open(QIODevice::WriteOnly) || f.write(p.toJson()) < 0 || !f.commit()) {
        if (error) {
            *error = f.errorString();
        }
        return false;
    }
    m_profiles.insert(p.id, p);
    qCInfo(lcProfiles) << "saved profile" << p.id;
    Q_EMIT profilesChanged();
    return true;
}

bool ProfileManager::remove(const QString &id)
{
    if (!m_profiles.contains(id)) {
        return false;
    }
    if (!QFile::remove(profilesDir() + QLatin1Char('/') + id + QStringLiteral(".json"))) {
        return false;
    }
    m_profiles.remove(id);
    Q_EMIT profilesChanged();
    return true;
}

QString ProfileManager::uniqueId(const QString &base) const
{
    QString clean;
    for (QChar c : base.toLower()) {
        if (c.isLetterOrNumber() && c.unicode() < 128) {
            clean += c;
        } else if (!clean.endsWith(QLatin1Char('-'))) {
            clean += QLatin1Char('-');
        }
    }
    while (clean.endsWith(QLatin1Char('-'))) {
        clean.chop(1);
    }
    while (clean.startsWith(QLatin1Char('-'))) {
        clean.remove(0, 1);
    }
    if (clean.isEmpty()) {
        clean = QStringLiteral("profile");
    }
    clean.truncate(56);
    QString id = clean;
    for (int i = 2; m_profiles.contains(id) || id == QLatin1String("keyboard"); ++i) {
        id = clean + QLatin1Char('-') + QString::number(i);
    }
    return id;
}
