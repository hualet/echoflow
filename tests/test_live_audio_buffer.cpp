// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "LiveAudioBuffer.h"

#include <array>

using namespace echoflow;

class TestLiveAudioBuffer : public QObject {
    Q_OBJECT

private slots:
    void appendS16leConvertsSamples();
    void markEofIsIdempotent();
};

void TestLiveAudioBuffer::appendS16leConvertsSamples() {
    LiveAudioBuffer buffer;
    const std::array<unsigned char, 8> bytes = {
        0x00, 0x00,
        0x00, 0x40,
        0x00, 0x80,
        0xff, 0x7f,
    };

    buffer.appendS16le(bytes.data(), bytes.size());

    const int64_t sampleCount = buffer.sampleCountForTest();
    const int eof = buffer.eofForTest();
    const std::vector<float> samples = buffer.samplesForTest();

    QCOMPARE(sampleCount, 4);
    QCOMPARE(eof, 0);
    QCOMPARE(samples.size(), size_t(4));
    QVERIFY(qAbs(samples[0] - 0.0f) < 0.0001f);
    QVERIFY(qAbs(samples[1] - 0.5f) < 0.0001f);
    QVERIFY(qAbs(samples[2] - -1.0f) < 0.0001f);
    QVERIFY(samples[3] > 0.9999f);
}

void TestLiveAudioBuffer::markEofIsIdempotent() {
    LiveAudioBuffer buffer;

    buffer.markEof();
    buffer.markEof();

    const int eof = buffer.eofForTest();

    QCOMPARE(eof, 1);
}

QTEST_MAIN(TestLiveAudioBuffer)
#include "test_live_audio_buffer.moc"
