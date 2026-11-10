#include "gamedetector.h"
#include "logging.h"
#include "processscanner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

GameDetector::GameDetector(const QString &gamesFile, QObject *parent)
    : QObject(parent)
    , m_file(gamesFile.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/keychrontray/games.json") : gamesFile)
    , m_scanner([] {
        return ProcessScanner::scan();
    })
{
    m_timer.setInterval(3000);
    connect(&m_timer, &QTimer::timeout, this, &GameDetector::scanNow);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] {
        // Editors replace the file; re-add the watch after reload.
        QTimer::singleShot(300, this, &GameDetector::reloadConfig);
    });
    reloadConfig();
}

void GameDetector::reloadConfig()
{
    QFile f(m_file);
    if (!f.open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("cannot read %1").arg(m_file);
        m_config = {};
    } else {
        QString err;
        if (auto c = GameConfig::fromJson(f.readAll(), &err)) {
            m_config = *c;
            m_error.clear();
            qCInfo(lcGames) << "loaded" << m_config.games.size() << "game rules";
        } else {
            m_error = err;
            qCWarning(lcGames) << "games.json:" << err << "(keeping previous rules)";
        }
    }
    if (QFile::exists(m_file) && !m_watcher.files().contains(m_file)) {
        m_watcher.addPath(m_file);
    }
    Q_EMIT configChanged();
    if (isEnabled()) {
        scanNow();
    }
}

bool GameDetector::saveConfig(const GameConfig &c, QString *error)
{
    QDir().mkpath(QFileInfo(m_file).path());
    QSaveFile f(m_file);
    if (!f.open(QIODevice::WriteOnly) || f.write(c.toJson()) < 0 || !f.commit()) {
        if (error) {
            *error = f.errorString();
        }
        return false;
    }
    m_config = c;
    m_error.clear();
    Q_EMIT configChanged();
    if (isEnabled()) {
        scanNow();
    }
    return true;
}

void GameDetector::setEnabled(bool on)
{
    if (on == isEnabled()) {
        return;
    }
    if (on) {
        m_timer.start();
        scanNow();
    } else {
        m_timer.stop();
        if (m_current) {
            m_current.reset();
            Q_EMIT gameChanged(m_current);
        }
    }
    qCInfo(lcGames) << "auto-switch" << (on ? "enabled" : "disabled");
}

void GameDetector::scanNow()
{
    auto m = GameMatcher::match(m_config, m_scanner());
    if (m && m->fallback && m_nameLookup) {
        const QString n = m_nameLookup(m->steamAppId);
        if (!n.isEmpty()) {
            m->label = n;
        }
    }
    const bool same = (m.has_value() == m_current.has_value()) && (!m || *m == *m_current);
    if (!same) {
        m_current = m;
        qCInfo(lcGames) << "game:" << (m ? QStringLiteral("%1 -> profile %2").arg(m->label, m->profile) : QStringLiteral("none"));
        Q_EMIT gameChanged(m_current);
    }
}
