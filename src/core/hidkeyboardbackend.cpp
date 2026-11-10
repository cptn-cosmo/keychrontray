#include "hidkeyboardbackend.h"
#include "logging.h"
#include "viaprotocol.h"

#include <QElapsedTimer>
#include <QThread>
#include <hidapi/hidapi.h>

HidKeyboardBackend::HidKeyboardBackend(QObject *parent)
    : KeyboardBackend(parent)
{
    if (hid_init() != 0) {
        qCWarning(lcHid) << "hid_init failed";
    }
}

HidKeyboardBackend::~HidKeyboardBackend()
{
    if (m_dev) {
        hid_close(m_dev); // no signals during destruction
        m_dev = nullptr;
    }
    hid_exit();
}

void HidKeyboardBackend::close()
{
    if (m_dev) {
        hid_close(m_dev);
        m_dev = nullptr;
        m_info = {};
        qCInfo(lcHid) << "keyboard disconnected";
        Q_EMIT connectedChanged(false);
    }
}

std::optional<QByteArray> HidKeyboardBackend::transact(const QByteArray &request, int timeoutMs)
{
    if (!m_dev) {
        return std::nullopt;
    }
    // Unnumbered report: hidraw expects a leading 0 report id.
    QByteArray out(1, '\0');
    out += request;
    if (hid_write(m_dev, reinterpret_cast<const unsigned char *>(out.constData()), size_t(out.size())) < 0) {
        qCWarning(lcHid) << "write failed:" << QString::fromWCharArray(hid_error(m_dev));
        close();
        return std::nullopt;
    }
    unsigned char buf[64];
    // Skip unrelated input reports (the firmware may push unsolicited data); bounded by the timeout.
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs) {
        const int n = hid_read_timeout(m_dev, buf, sizeof buf, int(timeoutMs - t.elapsed()));
        if (n < 0) {
            qCWarning(lcHid) << "read failed:" << QString::fromWCharArray(hid_error(m_dev));
            close();
            return std::nullopt;
        }
        if (n == 0) {
            break;
        }
        QByteArray reply(reinterpret_cast<const char *>(buf), n);
        if (reply[0] == request[0] || uint8_t(reply[0]) == Via::Unhandled) {
            return reply;
        }
    }
    return std::nullopt;
}

void HidKeyboardBackend::rescan()
{
    // Keep an existing, still-responsive handle.
    if (m_dev) {
        if (transact(Via::packet({Via::GetProtocolVersion}))) {
            return;
        }
        close();
    }

    hid_device_info *list = hid_enumerate(KeychronVid, 0);
    for (hid_device_info *d = list; d; d = d->next) {
        if (d->usage_page != Via::UsagePage || d->usage != Via::Usage) {
            continue;
        }
        const QString path = QString::fromLocal8Bit(d->path);
        hid_device *dev = hid_open_path(d->path);
        if (!dev) {
            qCWarning(lcHid) << "cannot open" << path << "(permissions? see README udev section)";
            continue;
        }
        m_dev = dev;
        const auto reply = transact(Via::packet({Via::GetProtocolVersion}), d->product_id == KeychronLinkPid ? 150 : 400);
        if (!m_dev) {
            continue; // closed on I/O error
        }
        const auto proto = reply ? Via::protocolVersionFromReply(*reply) : std::nullopt;
        if (!proto) {
            qCInfo(lcHid) << "no VIA response on" << path << QStringLiteral("(pid %1)").arg(d->product_id, 4, 16, QLatin1Char('0'))
                          << (d->product_id == KeychronLinkPid ? "- Keychron Link does not forward raw HID" : "");
            hid_close(m_dev);
            m_dev = nullptr;
            continue;
        }
        m_info.path = path;
        m_info.vendorId = d->vendor_id;
        m_info.productId = d->product_id;
        m_info.product = QString::fromWCharArray(d->product_string ? d->product_string : L"").simplified();
        m_info.viaProtocol = *proto;
        if (auto fw = transact(Via::packet({Via::KcGetFirmwareVersion}))) {
            m_info.firmware = Via::firmwareVersionFromReply(*fw);
        }
        qCInfo(lcHid) << "connected:" << m_info.product << path << "VIA protocol" << m_info.viaProtocol << "firmware" << m_info.firmware;
        break;
    }
    hid_free_enumeration(list);
    if (m_dev) {
        Q_EMIT connectedChanged(true);
    }
}

std::optional<LedState> HidKeyboardBackend::readState()
{
    LedState s;
    const auto get = [this](Via::RgbValue v) -> std::optional<QByteArray> {
        auto r = transact(Via::getRgbValue(v));
        return r ? Via::rgbValueFromReply(v, *r) : std::nullopt;
    };
    const auto b = get(Via::Brightness), e = get(Via::Effect), sp = get(Via::EffectSpeed), c = get(Via::Color);
    if (!b || !e || !sp || !c) {
        qCWarning(lcHid) << "readState failed";
        return std::nullopt;
    }
    s.brightness = uint8_t(b->at(0));
    s.effect = uint8_t(e->at(0));
    s.speed = uint8_t(sp->at(0));
    s.hue = uint8_t(c->at(0));
    s.saturation = uint8_t(c->at(1));
    return s;
}

bool HidKeyboardBackend::sendAll(std::initializer_list<QByteArray> cmds)
{
    for (const auto &c : cmds) {
        const auto r = transact(c);
        if (!r || !Via::isAck(c, *r)) {
            qCWarning(lcHid) << "command not acknowledged" << c.left(5).toHex(' ');
            return false;
        }
    }
    return true;
}

bool HidKeyboardBackend::apply(const LedState &s)
{
    if (!m_dev) {
        return false;
    }
    QElapsedTimer t;
    t.start();

    // Firmware quirks verified on a K10 HE (docs/PROTOCOL.md):
    //  1. Changing the effect reloads color/brightness/speed from the saved settings ~10-15 ms later, discarding
    //     values written in between. So: only send the effect if it changes, then wait before the other values.
    //  2. Brightness reads back one lower than written (scale8 rounding). LedState.brightness is the read-back
    //     value, so write the inverse; verified below.
    auto current = readState();
    if (!current) {
        return false;
    }
    if (current->effect != s.effect) {
        if (!sendAll({Via::setRgbValue(Via::Effect, s.effect)})) {
            return false;
        }
        QThread::msleep(EffectSettleMs);
    }
    uint8_t brightnessIn = uint8_t(qMin(255, (s.brightness * 256 + 254) / 255));
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!sendAll({Via::setRgbValue(Via::Color, s.hue, s.saturation), Via::setRgbValue(Via::Brightness, brightnessIn),
                      Via::setRgbValue(Via::EffectSpeed, s.speed)})) {
            return false;
        }
        current = readState();
        if (!current) {
            return false;
        }
        if (*current == s) {
            qCDebug(lcHid) << "applied" << s.toString() << "in" << t.elapsed() << "ms" << (attempt ? "(after retry)" : "");
            return true;
        }
        // Brightness off by one on boards with a different maximum: nudge. Anything else: the reset raced us; wait.
        if (current->brightness != s.brightness) {
            brightnessIn = uint8_t(qBound(0, int(brightnessIn) + (s.brightness > current->brightness ? 1 : -1), 255));
        }
        QThread::msleep(EffectSettleMs);
    }
    // Brightness 255 cannot be reached via VIA (max read-back is 254); accept that one difference.
    LedState relaxed = *current;
    relaxed.brightness = s.brightness;
    if (relaxed == s && s.brightness == 255 && current->brightness == 254) {
        return true;
    }
    qCWarning(lcHid) << "apply: keyboard reports" << current->toString() << "expected" << s.toString();
    return false;
}

bool HidKeyboardBackend::saveToEeprom()
{
    const auto c = Via::saveRgb();
    const auto r = transact(c);
    const bool ok = r && Via::isAck(c, *r);
    qCInfo(lcHid) << "save to EEPROM:" << (ok ? "ok" : "failed");
    return ok;
}
