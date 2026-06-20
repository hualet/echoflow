// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "AudioSegmenter.h"

#include <cstdint>
#include <vector>

using namespace echoflow;

namespace {

std::vector<int16_t> samples(double seconds, int16_t value) {
    return std::vector<int16_t>(static_cast<size_t>(seconds * 16000.0), value);
}

std::vector<AudioSegment> append(AudioSegmenter& segmenter, const std::vector<int16_t>& input) {
    return segmenter.append(input.data(), input.size());
}

}  // namespace

class TestAudioSegmenter : public QObject {
    Q_OBJECT

private slots:
    void defaultConfigPinsSegmenterParameters();
    void adaptiveNoiseFloorPreventsLowLevelBackgroundFromStartingSpeech();
    void emitsSegmentAfterTrailingSilence();
    void ignoresVeryShortNoise();
    void discardsShortNoiseBeforeLaterSpeech();
    void includesPreAndPostPadding();
    void forceSplitsLongSpeech();
    void flushReturnsOpenSegment();
};

void TestAudioSegmenter::defaultConfigPinsSegmenterParameters() {
    const AudioSegmenterConfig config;

    QCOMPARE(config.sampleRate, 16000);
    QCOMPARE(config.frameMs, 20);
    QCOMPARE(config.endSilenceMs, 800);
    QCOMPARE(config.minSegmentMs, 600);
    QCOMPARE(config.prePaddingMs, 200);
    QCOMPARE(config.postPaddingMs, 300);
    QCOMPARE(config.maxSegmentMs, 12000);
    QCOMPARE(config.speechRatio, 4.0);
    QCOMPARE(config.minSpeechRms, 600.0);
}

void TestAudioSegmenter::adaptiveNoiseFloorPreventsLowLevelBackgroundFromStartingSpeech() {
    AudioSegmenter segmenter(AudioSegmenterConfig{});

    append(segmenter, samples(1.0, 500));
    QCOMPARE(append(segmenter, samples(0.8, 1000)).size(), size_t(0));
    QCOMPARE(append(segmenter, samples(0.8, 0)).size(), size_t(0));
    QVERIFY(!segmenter.flush().has_value());

    QCOMPARE(append(segmenter, samples(0.8, 5000)).size(), size_t(0));
    const std::vector<AudioSegment> segments = append(segmenter, samples(0.8, 0));

    QCOMPARE(segments.size(), size_t(1));
    QCOMPARE(segments[0].sampleRate, 16000);
    QVERIFY(segments[0].sampleCount() >= size_t(17600));
}

void TestAudioSegmenter::emitsSegmentAfterTrailingSilence() {
    AudioSegmenter segmenter(AudioSegmenterConfig{});

    append(segmenter, samples(0.3, 0));
    QCOMPARE(append(segmenter, samples(1.0, 3000)).size(), size_t(0));
    const std::vector<AudioSegment> segments = append(segmenter, samples(0.8, 0));

    QCOMPARE(segments.size(), size_t(1));
    QCOMPARE(segments[0].sampleRate, 16000);
    QCOMPARE(segments[0].sampleCount(), size_t(24000));
    QVERIFY(qAbs(segments[0].durationSeconds() - 1.5) < 0.0001);
}

void TestAudioSegmenter::ignoresVeryShortNoise() {
    AudioSegmenter segmenter(AudioSegmenterConfig{});

    append(segmenter, samples(0.3, 0));
    QCOMPARE(append(segmenter, samples(0.2, 3000)).size(), size_t(0));
    QCOMPARE(append(segmenter, samples(0.8, 0)).size(), size_t(0));
    QVERIFY(!segmenter.flush().has_value());
}

void TestAudioSegmenter::discardsShortNoiseBeforeLaterSpeech() {
    AudioSegmenter segmenter(AudioSegmenterConfig{});

    QCOMPARE(append(segmenter, samples(0.2, 3000)).size(), size_t(0));
    QCOMPARE(append(segmenter, samples(3.0, 0)).size(), size_t(0));
    QCOMPARE(append(segmenter, samples(1.0, 3000)).size(), size_t(0));
    const std::vector<AudioSegment> segments = append(segmenter, samples(0.8, 0));

    QCOMPARE(segments.size(), size_t(1));
    QCOMPARE(segments[0].sampleCount(), size_t(24000));
    QCOMPARE(segments[0].samples.front(), int16_t(0));
    QCOMPARE(segments[0].samples[3200], int16_t(3000));
}

void TestAudioSegmenter::includesPreAndPostPadding() {
    AudioSegmenter segmenter(AudioSegmenterConfig{});

    append(segmenter, samples(0.5, 0));
    append(segmenter, samples(0.7, 3000));
    const std::vector<AudioSegment> segments = append(segmenter, samples(0.8, 0));

    QCOMPARE(segments.size(), size_t(1));
    QCOMPARE(segments[0].sampleCount(), size_t(19200));
    QCOMPARE(segments[0].samples.front(), int16_t(0));
    QCOMPARE(segments[0].samples[3200], int16_t(3000));
    QCOMPARE(segments[0].samples.back(), int16_t(0));
}

void TestAudioSegmenter::forceSplitsLongSpeech() {
    AudioSegmenterConfig config;
    config.maxSegmentMs = 1000;
    config.minSegmentMs = 200;
    AudioSegmenter segmenter(config);

    const std::vector<AudioSegment> segments = append(segmenter, samples(1.2, 3000));

    QCOMPARE(segments.size(), size_t(1));
    QCOMPARE(segments[0].sampleCount(), size_t(16000));
    const std::optional<AudioSegment> tail = segmenter.flush();
    QVERIFY(tail.has_value());
    QCOMPARE(tail->sampleCount(), size_t(3200));
}

void TestAudioSegmenter::flushReturnsOpenSegment() {
    AudioSegmenter segmenter(AudioSegmenterConfig{});

    append(segmenter, samples(0.3, 0));
    QCOMPARE(append(segmenter, samples(0.8, 3000)).size(), size_t(0));
    const std::optional<AudioSegment> segment = segmenter.flush();

    QVERIFY(segment.has_value());
    QCOMPARE(segment->sampleCount(), size_t(16000));
}

QTEST_GUILESS_MAIN(TestAudioSegmenter)
#include "test_audio_segmenter.moc"
