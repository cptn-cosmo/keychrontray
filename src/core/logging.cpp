#include "logging.h"

#include <QDateTime>
#include <QMetaObject>

Q_LOGGING_CATEGORY(lcHid, "keychrontray.hid", QtInfoMsg)
Q_LOGGING_CATEGORY(lcBattery, "keychrontray.battery", QtInfoMsg)
Q_LOGGING_CATEGORY(lcProfiles, "keychrontray.profiles", QtInfoMsg)
Q_LOGGING_CATEGORY(lcGames, "keychrontray.games", QtInfoMsg)
Q_LOGGING_CATEGORY(lcApp, "keychrontray.app", QtInfoMsg)

namespace {
QtMessageHandler g_previous = nullptr;

void handler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    if (g_previous) {
        g_previous(type, ctx, msg);
    }
    static const char *names[] = {"debug", "warning", "critical", "fatal", "info"};
    const QString line = QStringLiteral("%1 [%2] %3: %4")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")),
                                  QString::fromLatin1(names[type]),
                                  QString::fromLatin1(ctx.category ? ctx.category : "default"),
                                  msg);
    // May be called from any thread; marshal into the buffer's thread.
    QMetaObject::invokeMethod(LogBuffer::instance(), [line] { LogBuffer::instance()->append(line); }, Qt::AutoConnection);
}
}

LogBuffer *LogBuffer::instance()
{
    static LogBuffer buffer;
    return &buffer;
}

void LogBuffer::install()
{
    g_previous = qInstallMessageHandler(handler);
}

void LogBuffer::append(const QString &line)
{
    m_lines.append(line);
    if (m_lines.size() > MaxLines) {
        m_lines.removeFirst();
    }
    Q_EMIT lineAdded(line);
}
