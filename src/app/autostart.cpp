#include "autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

QString Autostart::path()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/autostart/io.github.keychrontray.desktop");
}

bool Autostart::isEnabled()
{
    return QFile::exists(path());
}

bool Autostart::setEnabled(bool on, QString *error)
{
    if (!on) {
        return !QFile::exists(path()) || QFile::remove(path());
    }
    QDir().mkpath(QFileInfo(path()).path());
    QSaveFile f(path());
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = f.errorString();
        }
        return false;
    }
    // Prefer the plain command when it resolves to this binary (packaged install), so the entry survives
    // reinstalls; otherwise use the absolute path (e.g. running from a build directory), quoted per the spec.
    const QString self = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
    const QString inPath = QStandardPaths::findExecutable(QStringLiteral("keychrontray"));
    const QString exec = (!inPath.isEmpty() && QFileInfo(inPath).canonicalFilePath() == self) ? QStringLiteral("keychrontray")
                                                                                              : QLatin1Char('"') + self + QLatin1Char('"');
    f.write(QStringLiteral("[Desktop Entry]\n"
                           "Type=Application\n"
                           "Name=Keychron Tray\n"
                           "Exec=%1\n"
                           "Icon=input-keyboard\n"
                           "X-KDE-autostart-phase=2\n"
                           "X-GNOME-Autostart-enabled=true\n")
                .arg(exec)
                .toUtf8());
    return f.commit();
}
