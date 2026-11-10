#pragma once

#include <QByteArray>
#include <cstdint>
#include <optional>

// VIA raw-HID packet encoding. Verified on a Keychron K10 HE (firmware v1.1.1), see docs/PROTOCOL.md.
namespace Via {
constexpr int ReportSize = 32;
constexpr uint16_t UsagePage = 0xFF60;
constexpr uint16_t Usage = 0x61;

enum Command : uint8_t {
    GetProtocolVersion = 0x01,
    CustomSetValue = 0x07,
    CustomGetValue = 0x08,
    CustomSave = 0x09,
    Unhandled = 0xFF,
    // Keychron extensions
    KcGetProtocolVersion = 0xA0,
    KcGetFirmwareVersion = 0xA1,
};

constexpr uint8_t RgbMatrixChannel = 0x03;
enum RgbValue : uint8_t {
    Brightness = 0x01,
    Effect = 0x02,
    EffectSpeed = 0x03,
    Color = 0x04,
};

// 32-byte payload (without the hidraw report-id prefix).
QByteArray packet(std::initializer_list<uint8_t> bytes);
QByteArray getRgbValue(RgbValue v);
QByteArray setRgbValue(RgbValue v, uint8_t a, uint8_t b = 0);
QByteArray saveRgb();

// True if `reply` acknowledges `request` (firmware echoes the command header back).
bool isAck(const QByteArray &request, const QByteArray &reply);
// For a get-value reply, returns the value bytes (offset 3..).
std::optional<QByteArray> rgbValueFromReply(RgbValue v, const QByteArray &reply);
std::optional<uint16_t> protocolVersionFromReply(const QByteArray &reply);
QString firmwareVersionFromReply(const QByteArray &reply);
}
