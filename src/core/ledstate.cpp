#include "ledstate.h"

namespace {
constexpr Effects::Entry kTable[] = {
    {0, "None"},
    {1, "Solid Color"},
    {2, "Breathing"},
    {3, "Band Spiral Val"},
    {4, "Cycle All"},
    {5, "Cycle Left Right"},
    {6, "Cycle Up Down"},
    {7, "Rainbow Moving Chevron"},
    {8, "Cycle Out In"},
    {9, "Cycle Out In Dual"},
    {10, "Cycle Pinwheel"},
    {11, "Cycle Spiral"},
    {12, "Dual Beacon"},
    {13, "Rainbow Beacon"},
    {14, "Jellybean Raindrops"},
    {15, "Pixel Rain"},
    {16, "Typing Heatmap"},
    {17, "Digital Rain"},
    {18, "Reactive Simple"},
    {19, "Reactive Multiwide"},
    {20, "Reactive Multinexus"},
    {21, "Splash"},
    {22, "Solid Splash"},
    {23, "Per Key RGB"}, // Keychron custom effects (firmware with KEYCHRON_RGB)
    {24, "Mixed RGB"},
};

QString normalize(const QString &s)
{
    QString out;
    for (QChar c : s) {
        if (c.isLetterOrNumber()) {
            out.append(c.toLower());
        }
    }
    return out;
}
}

QString LedState::toString() const
{
    return QStringLiteral("effect=%1(%2) hue=%3 sat=%4 brightness=%5 speed=%6")
        .arg(effect)
        .arg(Effects::nameFor(effect))
        .arg(hue)
        .arg(saturation)
        .arg(brightness)
        .arg(speed);
}

const Effects::Entry *Effects::table(int *count)
{
    *count = int(std::size(kTable));
    return kTable;
}

QString Effects::nameFor(uint8_t id)
{
    for (const auto &e : kTable) {
        if (e.id == id) {
            return QString::fromLatin1(e.name);
        }
    }
    return QStringLiteral("Effect %1").arg(id);
}

std::optional<uint8_t> Effects::parse(const QString &text)
{
    bool ok = false;
    const int n = text.trimmed().toInt(&ok);
    if (ok) {
        if (n < 0 || n > 255) {
            return std::nullopt;
        }
        return uint8_t(n);
    }
    const QString key = normalize(text);
    for (const auto &e : kTable) {
        if (normalize(QString::fromLatin1(e.name)) == key) {
            return e.id;
        }
    }
    return std::nullopt;
}
