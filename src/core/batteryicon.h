#pragma once

#include <QString>
#include <functional>

namespace BatteryIcon {
// Themed icon for a keyboard battery level, rounded to the nearest 10%:
//   input-keyboard-battery-NNN[-charging]  (Breeze, keyboard-specific)
//   battery-NNN[-charging]                 (fallback for themes without the keyboard icons)
//   input-keyboard                         (level unknown, or no matching icon)
// `hasIcon` is normally QIcon::hasThemeIcon; injectable for tests.
QString name(int percent, bool charging, const std::function<bool(const QString &)> &hasIcon);
}
