#include "devicemonitor.h"
#include "logging.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QSocketNotifier>
#include <libudev.h>

QString connectionLabel(ConnectionType t)
{
    switch (t) {
    case ConnectionType::UsbCable:
        return QStringLiteral("USB cable");
    case ConnectionType::Receiver24G:
        return QStringLiteral("2.4 GHz (Keychron Link)");
    case ConnectionType::Bluetooth:
        return QStringLiteral("Bluetooth");
    case ConnectionType::Disconnected:
        break;
    }
    return QStringLiteral("disconnected");
}

DeviceMonitor::DeviceMonitor(QObject *parent)
    : QObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(600); // let udev rules (permissions) settle
    connect(&m_debounce, &QTimer::timeout, this, [this] {
        refresh();
        Q_EMIT devicesChanged();
    });

    m_udev = udev_new();
    if (m_udev) {
        m_monitor = udev_monitor_new_from_netlink(m_udev, "udev");
        if (m_monitor) {
            udev_monitor_filter_add_match_subsystem_devtype(m_monitor, "hidraw", nullptr);
            udev_monitor_enable_receiving(m_monitor);
            m_notifier = new QSocketNotifier(udev_monitor_get_fd(m_monitor), QSocketNotifier::Read, this);
            connect(m_notifier, &QSocketNotifier::activated, this, &DeviceMonitor::onUdevEvent);
        }
    }
    if (!m_monitor) {
        qCWarning(lcHid) << "udev monitor unavailable; hotplug will only be detected on resume/manual rescan";
    }

    // BlueZ: any property change on a device (Connected) or added/removed interface.
    auto bus = QDBusConnection::systemBus();
    bus.connect(QStringLiteral("org.bluez"), QString(), QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"),
                this, SLOT(onBluezChanged()));
    bus.connect(QStringLiteral("org.bluez"), QStringLiteral("/"), QStringLiteral("org.freedesktop.DBus.ObjectManager"), QStringLiteral("InterfacesRemoved"),
                this, SLOT(onBluezChanged()));
    refresh();
}

DeviceMonitor::~DeviceMonitor()
{
    if (m_monitor) {
        udev_monitor_unref(m_monitor);
    }
    if (m_udev) {
        udev_unref(m_udev);
    }
}

void DeviceMonitor::onUdevEvent()
{
    udev_device *dev = udev_monitor_receive_device(m_monitor);
    if (!dev) {
        return;
    }
    udev_device *usb = udev_device_get_parent_with_subsystem_devtype(dev, "usb", "usb_device");
    const char *vid = usb ? udev_device_get_sysattr_value(usb, "idVendor") : nullptr;
    // On "remove" the parent attributes may be gone already; treat unknown as relevant.
    if (!vid || qstrcmp(vid, "3434") == 0) {
        qCDebug(lcHid) << "udev" << udev_device_get_action(dev) << udev_device_get_devnode(dev);
        m_debounce.start();
    }
    udev_device_unref(dev);
}

void DeviceMonitor::onBluezChanged()
{
    m_debounce.start();
}

void DeviceMonitor::refresh()
{
    scanUsb();
    scanBluez();
    recompute();
}

void DeviceMonitor::scanUsb()
{
    m_receiver = m_wired = false;
    if (!m_udev) {
        return;
    }
    udev_enumerate *e = udev_enumerate_new(m_udev);
    udev_enumerate_add_match_subsystem(e, "usb");
    udev_enumerate_add_match_property(e, "DEVTYPE", "usb_device");
    udev_enumerate_add_match_sysattr(e, "idVendor", "3434");
    udev_enumerate_scan_devices(e);
    udev_list_entry *entry;
    udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(e))
    {
        udev_device *d = udev_device_new_from_syspath(m_udev, udev_list_entry_get_name(entry));
        if (!d) {
            continue;
        }
        const QByteArray pid = QByteArray(udev_device_get_sysattr_value(d, "idProduct")).toLower();
        if (pid == "d030") {
            m_receiver = true;
        } else {
            m_wired = true;
        }
        udev_device_unref(d);
    }
    udev_enumerate_unref(e);
}

void DeviceMonitor::scanBluez()
{
    m_bluetooth = false;
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.bluez"), QStringLiteral("/"), QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                                                      QStringLiteral("GetManagedObjects"));
    QDBusMessage reply = QDBusConnection::systemBus().call(msg, QDBus::Block, 1000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return; // BlueZ not running
    }
    // a{oa{sa{sv}}}
    const QDBusArgument arg = reply.arguments().at(0).value<QDBusArgument>();
    arg.beginMap();
    while (!arg.atEnd()) {
        QDBusObjectPath path;
        QMap<QString, QVariantMap> ifaces;
        arg.beginMapEntry();
        arg >> path >> ifaces;
        arg.endMapEntry();
        const auto dev = ifaces.value(QStringLiteral("org.bluez.Device1"));
        if (dev.value(QStringLiteral("Connected")).toBool() && dev.value(QStringLiteral("Name")).toString().contains(QLatin1String("Keychron"), Qt::CaseInsensitive)) {
            m_bluetooth = true;
        }
    }
    arg.endMap();
}

void DeviceMonitor::recompute()
{
    ConnectionType t = ConnectionType::Disconnected;
    if (m_wired) {
        t = ConnectionType::UsbCable;
    } else if (m_bluetooth) {
        t = ConnectionType::Bluetooth;
    } else if (m_receiver) {
        t = ConnectionType::Receiver24G;
    }
    if (t != m_connection) {
        m_connection = t;
        qCInfo(lcHid) << "connection:" << connectionLabel(t);
        Q_EMIT connectionChanged(t);
    }
}
