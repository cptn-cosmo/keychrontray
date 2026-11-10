#pragma once

#include "profile.h"

#include <QFileSystemWatcher>
#include <QMap>
#include <QObject>
#include <QStringList>
#include <QTimer>

// Loads/saves profiles from <configDir>/profiles/*.json and ships defaults on first run.
// Watches the directory so hand edits are picked up automatically.
class ProfileManager : public QObject
{
    Q_OBJECT
public:
    // configDir defaults to ~/.config/keychrontray
    explicit ProfileManager(const QString &configDir = QString(), QObject *parent = nullptr);

    QString configDir() const { return m_configDir; }
    QString profilesDir() const;

    // Copies bundled default profiles (and games.json) if the profiles directory does not exist yet.
    // Returns true if defaults were installed.
    bool ensureDefaults();
    void reload();

    QList<Profile> profiles() const; // sorted by group, then name
    std::optional<Profile> profile(const QString &id) const;
    bool contains(const QString &id) const { return m_profiles.contains(id); }
    QStringList loadErrors() const { return m_errors; }

    bool save(const Profile &p, QString *error = nullptr);
    bool remove(const QString &id);
    // Picks a free id derived from `base`.
    QString uniqueId(const QString &base) const;

Q_SIGNALS:
    void profilesChanged();

private:
    QString m_configDir;
    QMap<QString, Profile> m_profiles;
    QStringList m_errors;
    QFileSystemWatcher m_watcher;
    QTimer m_reloadDebounce;
};
