#include "mockkeyboardbackend.h"

MockKeyboardBackend::MockKeyboardBackend(QObject *parent)
    : KeyboardBackend(parent)
{
    m_ram = LedState{1, 0, 255, 255, 15};
    m_eeprom = m_ram;
}

KeyboardInfo MockKeyboardBackend::info() const
{
    if (!m_connected) {
        return {};
    }
    return KeyboardInfo{QStringLiteral("Mock Keychron"), QStringLiteral("mock"), 0x3434, 0x0ea0, QStringLiteral("mock"), 0x000c};
}

std::optional<LedState> MockKeyboardBackend::readState()
{
    if (!m_connected) {
        return std::nullopt;
    }
    return m_ram;
}

bool MockKeyboardBackend::apply(const LedState &state)
{
    if (!m_connected || m_failWrites) {
        return false;
    }
    m_ram = state;
    m_applied.append(state);
    return true;
}

bool MockKeyboardBackend::saveToEeprom()
{
    if (!m_connected || m_failWrites) {
        return false;
    }
    m_eeprom = m_ram;
    ++m_eepromWrites;
    return true;
}

void MockKeyboardBackend::setConnected(bool connected)
{
    if (connected == m_connected) {
        return;
    }
    m_connected = connected;
    if (!connected) {
        // Real keyboards reload EEPROM on power cycle.
        m_ram = m_eeprom;
    }
    Q_EMIT connectedChanged(connected);
}

void MockKeyboardBackend::rescan()
{
    if (m_connected && !m_announced) {
        m_announced = true;
        Q_EMIT connectedChanged(true);
    }
}
