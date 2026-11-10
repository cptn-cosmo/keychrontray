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

    // Installs bundled default profiles (and games.json) on first run, and on later runs upgrades shipped
    // profiles the user has not edited, installs newly shipped ones, and leaves edited/deleted ones alone.
    // Returns true on first run.
    bool ensureDefaults();
    // Overwrites a profile with the bundled default (explicit user action).
    bool resetToDefault(const QString &id, QString *error = nullptr);
    static QStringList shippedIds();
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
    QString shippedStatePath() const;
    bool legacyInstall() const;

    QString m_configDir;
    QMap<QString, Profile> m_profiles;
    QStringList m_errors;
    QFileSystemWatcher m_watcher;
    QTimer m_reloadDebounce;
};
