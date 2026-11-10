#include "batteryprovider.h"
#include "logging.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusReply>

namespace {
const QString kService = QStringLiteral("org.freedesktop.UPower");
const QString kDeviceIface = QStringLiteral("org.freedesktop.UPower.Device");

// UPower Device.State values
constexpr uint StateCharging = 1;
constexpr uint StateFullyCharged = 4;
constexpr uint StatePendingCharge = 5;
}

BatteryProvider::BatteryProvider(QObject *parent)
    : QObject(parent)
{
    auto bus = QDBusConnection::systemBus();
    bus.connect(kService, QStringLiteral("/org/freedesktop/UPower"), kService, QStringLiteral("DeviceAdded"), this, SLOT(refresh()));
    bus.connect(kService, QStringLiteral("/org/freedesktop/UPower"), kService, QStringLiteral("DeviceRemoved"), this, SLOT(refresh()));
    refresh();
}

QString BatteryProvider::findKeyboard() const
{
    QDBusInterface upower(kService, QStringLiteral("/org/freedesktop/UPower"), kService, QDBusConnection::systemBus());
    QDBusReply<QList<QDBusObjectPath>> devices = upower.call(QStringLiteral("EnumerateDevices"));
    if (!devices.isValid()) {
        qCDebug(lcBattery) << "UPower unavailable:" << devices.error().message();
        return {};
    }
    for (const auto &p : devices.value()) {
        QDBusInterface dev(kService, p.path(), kDeviceIface, QDBusConnection::systemBus());
        const QString model = dev.property("Model").toString();
        const QString vendor = dev.property("Vendor").toString();
        if (model.contains(QLatin1String("Keychron"), Qt::CaseInsensitive) || vendor.contains(QLatin1String("Keychron"), Qt::CaseInsensitive)) {
            return p.path();
        }
    }
    return {};
}

void BatteryProvider::watch(const QString &path)
{
    if (path == m_watched) {
        return;
    }
    unwatch();
    m_watched = path;
    if (!path.isEmpty()) {
        QDBusConnection::systemBus().connect(kService, path, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"), this,
                                             SLOT(onDevicePropertiesChanged()));
    }
}

void BatteryProvider::unwatch()
{
    if (!m_watched.isEmpty()) {
        QDBusConnection::systemBus().disconnect(kService, m_watched, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"),
                                                this, SLOT(onDevicePropertiesChanged()));
        m_watched.clear();
    }
}

void BatteryProvider::refresh()
{
    watch(findKeyboard());
    onDevicePropertiesChanged();
}

void BatteryProvider::onDevicePropertiesChanged()
{
    BatteryStatus s;
    if (!m_watched.isEmpty()) {
        QDBusInterface dev(kService, m_watched, kDeviceIface, QDBusConnection::systemBus());
        const QVariant pct = dev.property("Percentage");
        const bool present = dev.property("IsPresent").toBool() || dev.property("Online").toBool() || pct.toDouble() > 0;
        if (pct.isValid() && present) {
            const uint state = dev.property("State").toUInt();
            s.available = true;
            s.percent = qBound(0, qRound(pct.toDouble()), 100);
            s.charging = state == StateCharging || state == StatePendingCharge || state == StateFullyCharged;
            s.source = m_watched;
        }
    }
    if (s.available != m_status.available || s.percent != m_status.percent || s.charging != m_status.charging) {
        m_status = s;
        qCInfo(lcBattery) << "battery:" << (s.available ? QStringLiteral("%1%%2").arg(s.percent).arg(s.charging ? QStringLiteral(" charging") : QString()) : QStringLiteral("unavailable"));
        Q_EMIT statusChanged(s);
    }
}
