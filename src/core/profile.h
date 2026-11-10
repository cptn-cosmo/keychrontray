#pragma once

#include "ledstate.h"

#include <QColor>
#include <QString>
#include <optional>

// A lighting profile, stored as one JSON file: ~/.config/keychrontray/profiles/<id>.json
//
// {
//   "name": "Endwalker",
//   "group": "Final Fantasy XIV",
//   "effect": "Solid Color",          // effect name or VIA id number
//   "color": "#d9b44a",               // hex, or {"hue": 0-255, "saturation": 0-255} in QMK units
//   "brightness": 80,                 // percent 0-100
//   "speed": 128,                     // 0-255
//   "description": "optional"
// }
//
// Only hue and saturation of "color" are used. Brightness is a separate field, matching the firmware.
// The K10 HE firmware supports one color per effect and no per-key colors over VIA (docs/PROTOCOL.md).
struct Profile {
    QString id;
    QString name;
    QString group;
    QString description;
    uint8_t effect = 1;
    QColor color = QColor(255, 255, 255);
    int brightnessPercent = 100;
    uint8_t speed = 128;

    LedState ledState() const;
    static Profile fromLedState(const QString &id, const QString &name, const LedState &s);

    static std::optional<Profile> fromJson(const QString &id, const QByteArray &json, QString *error = nullptr);
    QByteArray toJson() const;

    static bool isValidId(const QString &id);
    QString displayName() const { return name.isEmpty() ? id : name; }
};

namespace ColorConv {
uint8_t toQmkHue(const QColor &c);
QColor fromQmk(uint8_t hue, uint8_t saturation);
}
