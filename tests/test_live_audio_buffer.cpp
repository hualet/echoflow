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

    qwen_live_audio_t* live = buffer.get();
    pthread_mutex_lock(&live->mutex);
    QCOMPARE(live->n_samples, 4);
    QCOMPARE(live->eof, 0);
    QVERIFY(qAbs(live->samples[0] - 0.0f) < 0.0001f);
    QVERIFY(qAbs(live->samples[1] - 0.5f) < 0.0001f);
    QVERIFY(qAbs(live->samples[2] - -1.0f) < 0.0001f);
    QVERIFY(live->samples[3] > 0.9999f);
    pthread_mutex_unlock(&live->mutex);
}

void TestLiveAudioBuffer::markEofIsIdempotent() {
    LiveAudioBuffer buffer;

    buffer.markEof();
    buffer.markEof();

    qwen_live_audio_t* live = buffer.get();
    pthread_mutex_lock(&live->mutex);
    QCOMPARE(live->eof, 1);
    pthread_mutex_unlock(&live->mutex);
}

QTEST_MAIN(TestLiveAudioBuffer)
#include "test_live_audio_buffer.moc"
