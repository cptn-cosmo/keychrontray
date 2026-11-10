#pragma once

#include "gamerules.h"

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <functional>

// Periodically scans processes and reports the winning game (see GameMatcher for the priority rule).
// Linux has no unprivileged process-start notification (the proc connector needs CAP_NET_ADMIN), so this
// polls /proc. The default 3 s interval costs a few ms of CPU per scan.
class GameDetector : public QObject
{
    Q_OBJECT
public:
    using Scanner = std::function<QList<RunningProcess>()>;
    using NameLookup = std::function<QString(quint32 appId)>;

    // gamesFile defaults to ~/.config/keychrontray/games.json
    explicit GameDetector(const QString &gamesFile = QString(), QObject *parent = nullptr);

    void setScanner(Scanner s) { m_scanner = std::move(s); }
    void setNameLookup(NameLookup l) { m_nameLookup = std::move(l); }
    void setInterval(int ms) { m_timer.setInterval(ms); }

    void setEnabled(bool on);
    bool isEnabled() const { return m_timer.isActive(); }

    QString gamesFile() const { return m_file; }
    GameConfig config() const { return m_config; }
    QString loadError() const { return m_error; }
    bool saveConfig(const GameConfig &c, QString *error = nullptr);
    void reloadConfig();

    std::optional<GameMatch> current() const { return m_current; }

public Q_SLOTS:
    void scanNow();

Q_SIGNALS:
    void gameChanged(const std::optional<GameMatch> &match);
    void configChanged();

private:
    QString m_file;
    GameConfig m_config;
    QString m_error;
    QTimer m_timer;
    QFileSystemWatcher m_watcher;
    Scanner m_scanner;
    NameLookup m_nameLookup;
    std::optional<GameMatch> m_current;
};
