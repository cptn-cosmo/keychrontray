#pragma once

#include "gradient.h"
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
// or, as a multi-color gradient (per-key RGB, Keychron firmware with KEYCHRON_RGB):
//
// {
//   "name": "Endwalker",
//   "gradient": {
//     "colors": ["#e0b84a", "#ffffff", "#4aa3e0"],   // at least 2, left to right
//     "motion": "scroll",                             // "static" or "scroll" (moves left to right)
//     "cycleSeconds": 16                              // scroll: seconds per keyboard width
//   },
//   "brightness": 80
// }
//
// Only hue and saturation of colors are used; brightness is one global value (firmware limitation), so a
// gradient cannot fade to black. With "gradient", "effect" and "color" are ignored.
struct Profile {
    QString id;
    QString name;
    QString group;
    QString description;
    uint8_t effect = 1;
    QColor color = QColor(255, 255, 255);
    int brightnessPercent = 100;
    uint8_t speed = 128;
    std::optional<Gradient> gradient;

    bool isGradient() const { return gradient.has_value(); }
    // Global (VIA) part. For gradients: the per-key effect, brightness, and the first stop as base color.
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
