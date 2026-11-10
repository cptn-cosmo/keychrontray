#pragma once

#include "keyboardbackend.h"

#include <QSet>

struct hid_device_;

// VIA raw-HID backend over hidapi (hidraw). Finds Keychron (VID 0x3434) interfaces with usage page 0xFF60,
// probes them with a protocol-version query and uses the first one that answers. The Keychron Link 2.4 GHz
// receiver exposes the interface but does not forward VIA traffic (verified, see docs/PROTOCOL.md); it simply
// fails the probe and is skipped.
class HidKeyboardBackend : public KeyboardBackend
{
    Q_OBJECT
public:
    explicit HidKeyboardBackend(QObject *parent = nullptr);
    ~HidKeyboardBackend() override;

    bool isConnected() const override { return m_dev != nullptr; }
    KeyboardInfo info() const override { return m_info; }
    std::optional<LedState> readState() override;
    bool apply(const LedState &state) override;
    bool saveToEeprom() override;
    void rescan() override;

    static constexpr uint16_t KeychronVid = 0x3434;
    static constexpr uint16_t KeychronLinkPid = 0xD030;

private:
    std::optional<QByteArray> transact(const QByteArray &request, int timeoutMs = 250);
    bool sendAll(std::initializer_list<QByteArray> cmds);
    // After an effect change the firmware reloads color/brightness/speed; writes within ~15 ms are lost.
    static constexpr int EffectSettleMs = 40;
    void close();

    hid_device_ *m_dev = nullptr;
    KeyboardInfo m_info;
};
