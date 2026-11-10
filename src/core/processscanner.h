#pragma once

#include "gamerules.h"

// Reads the current user's processes from /proc (no root needed).
//
// Steam app ids come from:
//   - the Steam "reaper" launcher wrapper, whose cmdline has "AppId=<id>" (native and Proton games),
//   - the SteamAppId / SteamGameId environment variables of Wine/Proton (*.exe) processes.
namespace ProcessScanner {
QList<RunningProcess> scan(const QString &procRoot = QStringLiteral("/proc"));
// Exposed for tests.
quint32 appIdFromCmdline(const QStringList &argv);
quint32 appIdFromEnviron(const QByteArray &environ);
QString baseName(const QString &argv0);
}
