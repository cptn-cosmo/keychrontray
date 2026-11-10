#pragma once

#include "keyboardbackend.h"

#include <QList>

// In-memory keyboard for tests and for running without hardware (KEYCHRONTRAY_MOCK=1).
class MockKeyboardBackend : public KeyboardBackend
{
    Q_OBJECT
public:
    explicit MockKeyboardBackend(QObject *parent = nullptr);

    bool isConnected() const override { return m_connected; }
    KeyboardInfo info() const override;
    std::optional<LedState> readState() override;
    bool apply(const LedState &state) override;
    bool saveToEeprom() override;
    void rescan() override; // announces the initial connection once, like the HID backend
    int perKeyLedCount() const override { return m_perKeySupported ? int(m_perKey.size()) : 0; }
    QList<double> ledPositions() const override;
    std::optional<QList<HueSat>> readPerKey() override;
    bool writePerKey(int start, const QList<HueSat> &colors) override;
    void setPerKeySupported(bool on) { m_perKeySupported = on; }
    QList<HueSat> perKey() const { return m_perKey; }
    int perKeyWrites() const { return m_perKeyWrites; }

    // Test controls
    void setConnected(bool connected);
    void setFailWrites(bool fail) { m_failWrites = fail; }
    LedState ram() const { return m_ram; }
    LedState eeprom() const { return m_eeprom; }
    int applyCount() const { return m_applied.size(); }
    QList<LedState> applied() const { return m_applied; }
    int eepromWrites() const { return m_eepromWrites; }

private:
    bool m_connected = true;
    bool m_failWrites = false;
    bool m_announced = false;
    LedState m_ram;
    LedState m_eeprom;
    QList<LedState> m_applied;
    int m_eepromWrites = 0;
    bool m_perKeySupported = true;
    QList<HueSat> m_perKey = QList<HueSat>(104); // like a K10 HE: 104 LEDs, 21 columns
    int m_perKeyWrites = 0;
};
