#include "cli.h"
#include "dbusinterface.h"
#include "logging.h"
#include "traycontroller.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QIcon>
#include <QSocketNotifier>
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

namespace {
// SIGINT/SIGTERM -> clean quit (so the original lighting gets restored). Self-pipe pattern.
int g_sigFd[2];
void onSignal(int)
{
    char c = 1;
    [[maybe_unused]] auto n = ::write(g_sigFd[0], &c, 1);
}
}

int main(int argc, char **argv)
{
    QStringList args;
    for (int i = 0; i < argc; ++i) {
        args << QString::fromLocal8Bit(argv[i]);
    }

    QStringList cliArgs = args.mid(1);
    cliArgs.removeAll(QStringLiteral("--mock"));
    if (!cliArgs.isEmpty()) {
        QCoreApplication cli(argc, argv);
        QCoreApplication::setApplicationName(QStringLiteral("keychrontray"));
        return runCli(args).value_or(1);
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("keychrontray"));
    QApplication::setApplicationDisplayName(QStringLiteral("Keychron Tray"));
    QApplication::setDesktopFileName(QStringLiteral("io.github.keychrontray"));
    QApplication::setOrganizationDomain(QStringLiteral("keychrontray.github.io"));
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("input-keyboard")));
    LogBuffer::install();

    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerService(DBusService)) {
        qCCritical(lcApp) << "another instance is already running";
        return 1;
    }

    const bool mock = args.contains(QStringLiteral("--mock")) || qEnvironmentVariableIntValue("KEYCHRONTRAY_MOCK") == 1;
    TrayController tray(mock);
    new DBusInterface(&tray);
    bus.registerObject(DBusPath, &tray, QDBusConnection::ExportAdaptors);

    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_sigFd) == 0) {
        auto *sn = new QSocketNotifier(g_sigFd[1], QSocketNotifier::Read, &app);
        QObject::connect(sn, &QSocketNotifier::activated, &app, [] {
            char c;
            [[maybe_unused]] auto n = ::read(g_sigFd[1], &c, 1);
            qCInfo(lcApp) << "signal received, quitting";
            QCoreApplication::quit();
        });
        struct sigaction sa = {};
        sa.sa_handler = onSignal;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);
    }
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &tray, &TrayController::shutdown);

    qCInfo(lcApp) << "keychrontray started";
    return app.exec();
}
