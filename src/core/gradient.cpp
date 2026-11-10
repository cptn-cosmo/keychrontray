#include "gradient.h"
#include "profile.h"

#include <cmath>

HueSat HsvBlend::toHueSat(const QColor &c)
{
    return HueSat{ColorConv::toQmkHue(c), uint8_t(c.hsvSaturation())};
}

HueSat HsvBlend::mix(const QColor &a, const QColor &b, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    const HueSat ha = toHueSat(a), hb = toHueSat(b);
    // Achromatic stops (white/gray) have no hue: borrow the other side's so the blend only changes saturation.
    const double h0 = a.hsvHue() < 0 ? hb.h : ha.h;
    const double h1 = b.hsvHue() < 0 ? ha.h : hb.h;
    double dh = h1 - h0;
    if (dh > 128) {
        dh -= 256;
    } else if (dh < -128) {
        dh += 256;
    }
    int h = int(std::lround(h0 + dh * t)) % 256;
    if (h < 0) {
        h += 256;
    }
    const int s = int(std::lround(ha.s + (int(hb.s) - int(ha.s)) * t));
    return HueSat{uint8_t(h), uint8_t(std::clamp(s, 0, 255))};
}

HueSat Gradient::sample(double x, double phase) const
{
    if (stops.isEmpty()) {
        return {};
    }
    if (stops.size() == 1) {
        return HsvBlend::toHueSat(stops.first());
    }
    if (motion == Motion::Static) {
        const double p = std::clamp(x, 0.0, 1.0) * double(stops.size() - 1);
        const int i = std::min(int(p), int(stops.size()) - 2);
        return HsvBlend::mix(stops[i], stops[i + 1], p - i);
    }
    // Cyclic: A -> B -> ... -> A over one keyboard width; pattern(x - phase) moves right as phase grows.
    double u = std::fmod(x - phase, 1.0);
    if (u < 0) {
        u += 1.0;
    }
    const double p = u * double(stops.size());
    const int i = std::min(int(p), int(stops.size()) - 1);
    return HsvBlend::mix(stops[i], stops[(i + 1) % stops.size()], p - i);
}

QList<HueSat> Gradient::render(const QList<double> &xs, double phase) const
{
    QList<HueSat> out;
    out.reserve(xs.size());
    for (double x : xs) {
        out.append(sample(x, phase));
    }
    return out;
}

double Gradient::phaseAt(qint64 msecs) const
{
    if (motion != Motion::Scroll || cycleSeconds <= 0) {
        return 0.0;
    }
    return std::fmod(double(msecs) / (cycleSeconds * 1000.0), 1.0);
}
