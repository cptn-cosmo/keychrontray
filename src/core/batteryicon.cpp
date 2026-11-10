#include "batteryicon.h"

#include <QStringList>

QString BatteryIcon::name(int percent, bool charging, const std::function<bool(const QString &)> &hasIcon)
{
    if (percent >= 0) {
        const int level = qBound(0, (qMin(percent, 100) + 5) / 10 * 10, 100);
        const QString suffix = QStringLiteral("%1%2").arg(level, 3, 10, QLatin1Char('0')).arg(charging ? QStringLiteral("-charging") : QString());
        for (const QString &prefix : {QStringLiteral("input-keyboard-battery-"), QStringLiteral("battery-")}) {
            if (hasIcon(prefix + suffix)) {
                return prefix + suffix;
            }
        }
    }
    return QStringLiteral("input-keyboard");
}
