#pragma once

#include <QString>

// ~/.config/autostart/io.github.keychrontray.desktop
namespace Autostart {
QString path();
bool isEnabled();
bool setEnabled(bool on, QString *error = nullptr);
}
