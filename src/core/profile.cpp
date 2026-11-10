#include "profile.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <cmath>

uint8_t ColorConv::toQmkHue(const QColor &c)
{
    const int h = c.hsvHue(); // 0-359, -1 for gray
    if (h < 0) {
        return 0;
    }
    return uint8_t(int(std::lround(h * 256.0 / 360.0)) % 256);
}

QColor ColorConv::fromQmk(uint8_t hue, uint8_t saturation)
{
    return QColor::fromHsv(int(std::lround(hue * 360.0 / 256.0)) % 360, saturation, 255);
}

LedState Profile::ledState() const
{
    LedState s;
    s.effect = effect;
    s.hue = ColorConv::toQmkHue(color);
    s.saturation = uint8_t(color.hsvSaturation());
    s.brightness = uint8_t(std::lround(qBound(0, brightnessPercent, 100) * 255.0 / 100.0));
    s.speed = speed;
    return s;
}

Profile Profile::fromLedState(const QString &id, const QString &name, const LedState &s)
{
    Profile p;
    p.id = id;
    p.name = name;
    p.effect = s.effect;
    p.color = ColorConv::fromQmk(s.hue, s.saturation);
    p.brightnessPercent = int(std::lround(s.brightness * 100.0 / 255.0));
    p.speed = s.speed;
    return p;
}

bool Profile::isValidId(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^[a-z0-9][a-z0-9._-]{0,63}$"));
    return re.match(id).hasMatch();
}

std::optional<Profile> Profile::fromJson(const QString &id, const QByteArray &json, QString *error)
{
    auto fail = [error](const QString &msg) -> std::optional<Profile> {
        if (error) {
            *error = msg;
        }
        return std::nullopt;
    };
    if (!isValidId(id)) {
        return fail(QStringLiteral("invalid profile id '%1' (use lowercase letters, digits, '-', '_', '.')").arg(id));
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError) {
        return fail(QStringLiteral("JSON error at offset %1: %2").arg(pe.offset).arg(pe.errorString()));
    }
    if (!doc.isObject()) {
        return fail(QStringLiteral("top level must be an object"));
    }
    const QJsonObject o = doc.object();
    Profile p;
    p.id = id;
    p.name = o.value(QLatin1String("name")).toString(id);
    p.group = o.value(QLatin1String("group")).toString();
    p.description = o.value(QLatin1String("description")).toString();

    const QJsonValue eff = o.value(QLatin1String("effect"));
    if (!eff.isUndefined()) {
        const auto e = Effects::parse(eff.isDouble() ? QString::number(eff.toInt(-1)) : eff.toString());
        if (!e) {
            return fail(QStringLiteral("unknown effect '%1'").arg(eff.isDouble() ? QString::number(eff.toDouble()) : eff.toString()));
        }
        p.effect = *e;
    }

    const QJsonValue col = o.value(QLatin1String("color"));
    if (col.isString()) {
        QColor c = QColor::fromString(col.toString());
        if (!c.isValid()) {
            return fail(QStringLiteral("invalid color '%1' (expected #rrggbb)").arg(col.toString()));
        }
        p.color = c;
    } else if (col.isObject()) {
        const QJsonObject co = col.toObject();
        const int h = co.value(QLatin1String("hue")).toInt(-1);
        const int s = co.value(QLatin1String("saturation")).toInt(255);
        if (h < 0 || h > 255 || s < 0 || s > 255) {
            return fail(QStringLiteral("color hue/saturation must be 0-255"));
        }
        p.color = ColorConv::fromQmk(uint8_t(h), uint8_t(s));
    } else if (!col.isUndefined()) {
        return fail(QStringLiteral("color must be \"#rrggbb\" or {\"hue\":..,\"saturation\":..}"));
    }

    const QJsonValue br = o.value(QLatin1String("brightness"));
    if (!br.isUndefined()) {
        if (!br.isDouble() || br.toDouble() < 0 || br.toDouble() > 100) {
            return fail(QStringLiteral("brightness must be a number 0-100 (percent)"));
        }
        p.brightnessPercent = int(std::lround(br.toDouble()));
    }
    const QJsonValue sp = o.value(QLatin1String("speed"));
    if (!sp.isUndefined()) {
        if (!sp.isDouble() || sp.toDouble() < 0 || sp.toDouble() > 255) {
            return fail(QStringLiteral("speed must be a number 0-255"));
        }
        p.speed = uint8_t(std::lround(sp.toDouble()));
    }
    return p;
}

QByteArray Profile::toJson() const
{
    QJsonObject o;
    o.insert(QLatin1String("name"), name);
    if (!group.isEmpty()) {
        o.insert(QLatin1String("group"), group);
    }
    if (!description.isEmpty()) {
        o.insert(QLatin1String("description"), description);
    }
    o.insert(QLatin1String("effect"), Effects::nameFor(effect));
    o.insert(QLatin1String("color"), color.name(QColor::HexRgb));
    o.insert(QLatin1String("brightness"), brightnessPercent);
    o.insert(QLatin1String("speed"), speed);
    return QJsonDocument(o).toJson(QJsonDocument::Indented);
}
