#pragma once

#include "ledstate.h"

#include <QObject>
#include <QString>
#include <optional>

struct KeyboardInfo {
    QString product; // e.g. "Keychron K10 HE"
    QString path; // hidraw node
    uint16_t vendorId = 0;
    uint16_t productId = 0;
    QString firmware; // e.g. "v1.1.1 2025-06-17-10:45:33"
    uint16_t viaProtocol = 0;
};

// Talks to the keyboard's lighting. Implementations: HidKeyboardBackend (real), MockKeyboardBackend (tests).
class KeyboardBackend : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~KeyboardBackend() override = default;

    virtual bool isConnected() const = 0;
    virtual KeyboardInfo info() const = 0;

    // Reads the current (RAM) lighting state.
    virtual std::optional<LedState> readState() = 0;
    // Applies to RAM only (no EEPROM write). Returns false on failure.
    virtual bool apply(const LedState &state) = 0;
    // Persists the current RAM lighting state to EEPROM. Wears flash; only on explicit user request.
    virtual bool saveToEeprom() = 0;

    // Re-enumerates devices (call after hotplug/resume).
    virtual void rescan() = 0;

Q_SIGNALS:
    void connectedChanged(bool connected);
};
