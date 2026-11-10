#pragma once

#include <QObject>
#include <QTimer>

class QSocketNotifier;
struct udev;
struct udev_monitor;

enum class ConnectionType {
    Disconnected,
    UsbCable,
    Receiver24G, // Keychron Link present; keyboard may or may not be on
    Bluetooth,
};
QString connectionLabel(ConnectionType t);

// Watches udev (hidraw add/remove) and BlueZ to determine how the keyboard is attached.
// Emits devicesChanged() (debounced) on any relevant hotplug, so backends can rescan.
class DeviceMonitor : public QObject
{
    Q_OBJECT
public:
    explicit DeviceMonitor(QObject *parent = nullptr);
    ~DeviceMonitor() override;

    ConnectionType connection() const { return m_connection; }
    bool receiverPresent() const { return m_receiver; }
    bool wiredPresent() const { return m_wired; }
    bool bluetoothConnected() const { return m_bluetooth; }

public Q_SLOTS:
    void refresh();

Q_SIGNALS:
    void devicesChanged();
    void connectionChanged(ConnectionType type);

private Q_SLOTS:
    void onUdevEvent();
    void onBluezChanged();

private:
    void scanUsb();
    void scanBluez();
    void recompute();

    udev *m_udev = nullptr;
    udev_monitor *m_monitor = nullptr;
    QSocketNotifier *m_notifier = nullptr;
    QTimer m_debounce;
    bool m_receiver = false;
    bool m_wired = false;
    bool m_bluetooth = false;
    ConnectionType m_connection = ConnectionType::Disconnected;
};
