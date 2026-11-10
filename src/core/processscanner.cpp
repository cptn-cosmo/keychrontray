#include "processscanner.h"

#include <QDir>
#include <QFile>
#include <sys/stat.h>
#include <unistd.h>

namespace {
QByteArray readSmall(const QString &path, qint64 max = 64 * 1024)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Unbuffered)) {
        return {};
    }
    return f.read(max);
}
}

quint32 ProcessScanner::appIdFromCmdline(const QStringList &argv)
{
    // reaper SteamLaunch AppId=1086940 -- ...
    for (const QString &a : argv) {
        if (a == QLatin1String("--")) {
            break;
        }
        if (a.startsWith(QLatin1String("AppId="))) {
            return a.mid(6).toUInt();
        }
    }
    return 0;
}

quint32 ProcessScanner::appIdFromEnviron(const QByteArray &environ)
{
    quint32 gameId = 0;
    for (const QByteArray &kv : environ.split('\0')) {
        if (kv.startsWith("SteamAppId=")) {
            const quint32 v = kv.mid(11).toUInt();
            if (v) {
                return v;
            }
        } else if (kv.startsWith("SteamGameId=")) {
            gameId = kv.mid(12).toUInt(); // non-Steam shortcuts get large ids here; still useful as a fallback
        }
    }
    return gameId <= 0x7FFFFFFF ? gameId : 0;
}

QString ProcessScanner::baseName(const QString &argv0)
{
    const qsizetype i = std::max(argv0.lastIndexOf(QLatin1Char('/')), argv0.lastIndexOf(QLatin1Char('\\')));
    return i >= 0 ? argv0.mid(i + 1) : argv0;
}

QList<RunningProcess> ProcessScanner::scan(const QString &procRoot)
{
    QList<RunningProcess> out;
    const uid_t me = getuid();
    const QStringList pids = QDir(procRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &pidStr : pids) {
        bool ok = false;
        const int pid = pidStr.toInt(&ok);
        if (!ok) {
            continue;
        }
        const QString base = procRoot + QLatin1Char('/') + pidStr;
        struct stat st;
        if (procRoot == QLatin1String("/proc") && (::stat(QFile::encodeName(base).constData(), &st) != 0 || st.st_uid != me)) {
            continue;
        }
        const QByteArray cmdline = readSmall(base + QStringLiteral("/cmdline"));
        if (cmdline.isEmpty()) {
            continue; // kernel thread or gone
        }
        QStringList argv;
        for (const QByteArray &a : cmdline.split('\0')) {
            if (!a.isEmpty()) {
                argv << QString::fromLocal8Bit(a);
            }
        }
        if (argv.isEmpty()) {
            continue;
        }
        RunningProcess p;
        p.pid = pid;
        p.comm = QString::fromLocal8Bit(readSmall(base + QStringLiteral("/comm"), 64)).trimmed();
        p.name = ProcessScanner::baseName(argv.first());
        p.steamAppId = appIdFromCmdline(argv);
        if (!p.steamAppId && p.name.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive)) {
            p.steamAppId = appIdFromEnviron(readSmall(base + QStringLiteral("/environ"), 256 * 1024));
        }
        // stat: "pid (comm) state ppid ..." - comm may contain spaces/parens, so split after the last ')'.
        const QByteArray stat = readSmall(base + QStringLiteral("/stat"), 4096);
        const qsizetype rp = stat.lastIndexOf(')');
        if (rp > 0) {
            const QList<QByteArray> fields = stat.mid(rp + 2).split(' ');
            // fields[0] is field 3 (state); starttime is field 22 -> index 19
            if (fields.size() > 19) {
                p.startTime = fields[19].toLongLong();
            }
        }
        out.append(p);
    }
    return out;
}
