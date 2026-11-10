#pragma once

#include <QColor>
#include <QList>
#include <cstdint>
#include <optional>

// Hue/saturation of one LED in QMK units (0-255). The per-key solid mode of Keychron's firmware uses the
// keyboard's global brightness, so per-LED value/brightness is not representable (no "black" keys).
struct HueSat {
    uint8_t h = 0;
    uint8_t s = 0;
    bool operator==(const HueSat &) const = default;
};

// A multi-color left-to-right gradient.
//
// static: stops spread evenly from the left edge (first color) to the right edge (last color).
// scroll: the stops repeat cyclically (A -> B -> ... -> A) across the keyboard width and the pattern moves
//         left to right, one full keyboard width every `cycleSeconds`.
//
// Colors are blended in HSV along the shorter way round the hue circle; add an intermediate stop to steer
// (e.g. gold -> white -> blue instead of gold -> green -> blue).
struct Gradient {
    enum class Motion { Static, Scroll };

    QList<QColor> stops;
    Motion motion = Motion::Static;
    double cycleSeconds = 12.0;

    bool operator==(const Gradient &o) const
    {
        return stops == o.stops && motion == o.motion && qFuzzyCompare(cycleSeconds, o.cycleSeconds);
    }

    // Color at horizontal position x (0 = left edge, 1 = right edge) for animation phase (0..1).
    HueSat sample(double x, double phase = 0.0) const;
    // One frame for LEDs at the given x positions.
    QList<HueSat> render(const QList<double> &xs, double phase = 0.0) const;
    // Phase for a point in time.
    double phaseAt(qint64 msecs) const;
};

namespace HsvBlend {
// Blend two colors in QMK hue/sat space along the shorter hue arc. t in 0..1.
HueSat mix(const QColor &a, const QColor &b, double t);
HueSat toHueSat(const QColor &c);
}
