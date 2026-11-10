#pragma once

#include <QObject>
#include <QString>

struct BatteryStatus {
    bool available = false;
    int percent = -1;
    bool charging = false;
    QString source; // UPower device path, for diagnostics
};

// Reads the keyboard battery from UPower (which aggregates BlueZ Battery1 and kernel HID battery reports).
// Signal-driven: listens to UPower DeviceAdded/DeviceRemoved and the device's PropertiesChanged.
// Matches devices whose Model or Vendor contains "Keychron".
class BatteryProvider : public QObject
{
    Q_OBJECT
public:
    explicit BatteryProvider(QObject *parent = nullptr);

    BatteryStatus status() const { return m_status; }

public Q_SLOTS:
    void refresh();

Q_SIGNALS:
    void statusChanged(const BatteryStatus &status);

private Q_SLOTS:
    void onDevicePropertiesChanged();

private:
    void watch(const QString &path);
    void unwatch();
    QString findKeyboard() const;

    QString m_watched;
    BatteryStatus m_status;
};
