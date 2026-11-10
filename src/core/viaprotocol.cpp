#include "viaprotocol.h"

#include <QString>

namespace Via {

QByteArray packet(std::initializer_list<uint8_t> bytes)
{
    QByteArray p(ReportSize, '\0');
    int i = 0;
    for (uint8_t b : bytes) {
        p[i++] = char(b);
    }
    return p;
}

QByteArray getRgbValue(RgbValue v)
{
    return packet({CustomGetValue, RgbMatrixChannel, v});
}

QByteArray setRgbValue(RgbValue v, uint8_t a, uint8_t b)
{
    if (v == Color) {
        return packet({CustomSetValue, RgbMatrixChannel, v, a, b});
    }
    return packet({CustomSetValue, RgbMatrixChannel, v, a});
}

QByteArray saveRgb()
{
    return packet({CustomSave, RgbMatrixChannel});
}

bool isAck(const QByteArray &request, const QByteArray &reply)
{
    if (reply.size() < 3 || request.size() < 3) {
        return false;
    }
    if (uint8_t(reply[0]) == Unhandled) {
        return false;
    }
    const int header = (uint8_t(request[0]) == CustomSave) ? 2 : 3;
    return reply.left(header) == request.left(header);
}

std::optional<QByteArray> rgbValueFromReply(RgbValue v, const QByteArray &reply)
{
    if (reply.size() < 5 || uint8_t(reply[0]) != CustomGetValue || uint8_t(reply[1]) != RgbMatrixChannel
        || uint8_t(reply[2]) != v) {
        return std::nullopt;
    }
    return reply.mid(3, 2);
}

std::optional<uint16_t> protocolVersionFromReply(const QByteArray &reply)
{
    if (reply.size() < 3 || uint8_t(reply[0]) != GetProtocolVersion) {
        return std::nullopt;
    }
    return uint16_t((uint8_t(reply[1]) << 8) | uint8_t(reply[2]));
}

QString firmwareVersionFromReply(const QByteArray &reply)
{
    if (reply.isEmpty() || uint8_t(reply[0]) != KcGetFirmwareVersion) {
        return {};
    }
    QByteArray s = reply.mid(1);
    const int nul = s.indexOf('\0');
    if (nul >= 0) {
        s.truncate(nul);
    }
    return QString::fromLatin1(s);
}
}
