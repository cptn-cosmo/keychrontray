#pragma once

#include <QLoggingCategory>
#include <QObject>
#include <QStringList>

Q_DECLARE_LOGGING_CATEGORY(lcHid)
Q_DECLARE_LOGGING_CATEGORY(lcBattery)
Q_DECLARE_LOGGING_CATEGORY(lcProfiles)
Q_DECLARE_LOGGING_CATEGORY(lcGames)
Q_DECLARE_LOGGING_CATEGORY(lcApp)

// Keeps the last N log lines in memory for the diagnostics tab.
class LogBuffer : public QObject
{
    Q_OBJECT
public:
    static LogBuffer *instance();
    // Installs a Qt message handler that forwards to the previous handler and records lines.
    static void install();

    QStringList lines() const { return m_lines; }
    void append(const QString &line);

Q_SIGNALS:
    void lineAdded(const QString &line);

private:
    QStringList m_lines;
    static constexpr int MaxLines = 2000;
};
