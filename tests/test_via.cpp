#include "viaprotocol.h"

#include <QTest>

class TestVia : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void packetsMatchVerifiedBytes()
    {
        // Byte sequences verified on a K10 HE, see docs/PROTOCOL.md
        QCOMPARE(Via::getRgbValue(Via::Brightness).left(4), QByteArray::fromHex("08030100"));
        QCOMPARE(Via::setRgbValue(Via::Effect, 1).left(5), QByteArray::fromHex("0703020100"));
        QCOMPARE(Via::setRgbValue(Via::Color, 0xaa, 0xff).left(6), QByteArray::fromHex("070304aaff00"));
        QCOMPARE(Via::setRgbValue(Via::Brightness, 0xc8).left(5), QByteArray::fromHex("070301c800"));
        QCOMPARE(Via::saveRgb().left(3), QByteArray::fromHex("090300"));
        QCOMPARE(Via::setRgbValue(Via::EffectSpeed, 1).size(), Via::ReportSize);
    }

    void ackDetection()
    {
        const auto req = Via::setRgbValue(Via::Color, 0xaa, 0xff);
        QVERIFY(Via::isAck(req, req));
        QVERIFY(!Via::isAck(req, Via::packet({0xff})));
        QVERIFY(!Via::isAck(req, Via::packet({0x07, 0x03, 0x01})));
        QVERIFY(Via::isAck(Via::saveRgb(), Via::packet({0x09, 0x03})));
        QVERIFY(!Via::isAck(req, QByteArray()));
    }

    void parseReplies()
    {
        auto v = Via::rgbValueFromReply(Via::Color, QByteArray::fromHex("08030400ff0000"));
        QVERIFY(v);
        QCOMPARE(uint8_t(v->at(0)), 0);
        QCOMPARE(uint8_t(v->at(1)), 0xff);
        QVERIFY(!Via::rgbValueFromReply(Via::Brightness, QByteArray::fromHex("08030400ff")));
        QCOMPARE(Via::protocolVersionFromReply(QByteArray::fromHex("01000c")).value(), uint16_t(12));
        QCOMPARE(Via::firmwareVersionFromReply(QByteArray("\xa1v1.1.1 2025-06-17\0\0", 21)), QStringLiteral("v1.1.1 2025-06-17"));
    }
};

QTEST_GUILESS_MAIN(TestVia)
#include "test_via.moc"
