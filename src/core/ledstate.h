#pragma once

#include <QString>
#include <cstdint>
#include <optional>

// Lighting state as the firmware sees it (VIA RGB-matrix channel, all values 0-255).
struct LedState {
    uint8_t effect = 1; // VIA effect id, 0 = off
    uint8_t hue = 0; // QMK hue 0-255
    uint8_t saturation = 255;
    uint8_t brightness = 255;
    uint8_t speed = 128;

    bool operator==(const LedState &) const = default;
    QString toString() const;
};

// RGB-matrix effect table for the Keychron K10 HE (firmware hall_effect_playground, via_json/k10_he_ansi.json).
// Other boards may number effects differently; profiles can always use the numeric id.
namespace Effects {
// Keychron custom effect that shows the per-key color table (verified on a K10 HE: VIA effect 23).
constexpr uint8_t PerKeyRgb = 23;
struct Entry {
    uint8_t id;
    const char *name;
};
const Entry *table(int *count);
QString nameFor(uint8_t id);
// Accepts "Solid Color", "solid_color", "solidcolor" (case-insensitive) or a decimal number.
std::optional<uint8_t> parse(const QString &text);
}
