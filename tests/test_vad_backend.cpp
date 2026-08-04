// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SileroVadBackend.h"

#include <QTemporaryDir>
#include <QTest>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

using namespace echoflow;

namespace {

// Silero v6.2.0 reports one probability per 512-sample (32 ms) window.
constexpr int kWindow = 512;
constexpr int kSamplesPerMs = 16;

// Build a probability vector from frame-level labels so tests stay readable.
// 's' marks a speech frame (at the start threshold, so >= opens a segment),
// 'e' an end-of-speech frame (just below the end threshold, so it accrues
// silence), 'm' a mid frame (strictly between the two thresholds, holding
// state), and '.' silence at the noise floor.
std::vector<float> probsFromString(const std::string& frames,
                                   float startThreshold, float endThreshold)
{
    const float mid = (startThreshold + endThreshold) * 0.5f;
    const float belowEnd = endThreshold * 0.5f;
    std::vector<float> probs;
    probs.reserve(frames.size());
    for (char c : frames) {
        switch (c) {
            case 's': probs.push_back(startThreshold); break;
            case 'm': probs.push_back(mid); break;
            case 'e': probs.push_back(belowEnd); break;
            default:  probs.push_back(0.0f); break;
        }
    }
    return probs;
}

}  // namespace

class TestVadBackend : public QObject {
    Q_OBJECT

private slots:
    void missingModelFailsExplicitly();
    void corruptModelFailsExplicitly();
    void rejectsInvalidParameters();
    void rejectsEndAboveStart();

    void weakSpeechBetweenThresholdsOpens();
    void pauseAboveNoiseClosesAfterSilence();
    void briefWobbleDoesNotFragment();
    void openSpeechEndsAtTotalSamples();
    void tooShortClosedSegmentDropped();
    void defaultsDoNotMergeAdjacentGaps();
};

void TestVadBackend::missingModelFailsExplicitly()
{
    QVERIFY_EXCEPTION_THROWN(
        SileroVadBackend("/definitely/missing/ggml-silero.bin", 0.15f, 0.10f, 100, 300),
        std::runtime_error);
}

void TestVadBackend::corruptModelFailsExplicitly()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const std::filesystem::path path =
        std::filesystem::path(dir.path().toStdString()) / "ggml-silero.bin";
    std::ofstream(path) << "not a model";
    QVERIFY_EXCEPTION_THROWN(
        SileroVadBackend(path, 0.15f, 0.10f, 100, 300),
        std::runtime_error);
}

void TestVadBackend::rejectsInvalidParameters()
{
    QVERIFY_EXCEPTION_THROWN(
        SileroVadBackend("/unused", 0.0f, 0.05f, 100, 300), std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(
        SileroVadBackend("/unused", 0.15f, 0.0f, 100, 300), std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(
        SileroVadBackend("/unused", 0.15f, 0.10f, 0, 300), std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(
        SileroVadBackend("/unused", 0.15f, 0.10f, 100, 0), std::invalid_argument);
}

void TestVadBackend::rejectsEndAboveStart()
{
    QVERIFY_EXCEPTION_THROWN(
        SileroVadBackend("/unused", 0.1f, 0.2f, 100, 300), std::invalid_argument);
}

// A sentence whose peak sits at 0.15 (between the old 0.3 start gate and the
// noise floor) must still open a segment under a decoupled start threshold.
void TestVadBackend::weakSpeechBetweenThresholdsOpens()
{
    const float start = 0.15f;
    const float end = 0.10f;
    // 1s silence, 1s speech at the start threshold, then enough below-end
    // frames to close (>= 300ms).
    const int silenceFrames = 1000 * kWindow / (kSamplesPerMs * 1000);
    const int speechFrames = 1000 * kWindow / (kSamplesPerMs * 1000);
    const int closeFrames = 300 * kWindow / (kSamplesPerMs * 1000) + 1;

    std::string frames(silenceFrames, '.');
    frames += std::string(speechFrames, 's');
    frames += std::string(closeFrames, 'e');

    const auto probs = probsFromString(frames, start, end);
    const size_t total = static_cast<size_t>(probs.size()) * kWindow;
    const auto segments = segmentByDualThreshold(
        probs.data(), static_cast<int>(probs.size()), kWindow, total, start, end, 100, 300);

    QCOMPARE(segments.size(), size_t(1));
    QVERIFY(segments[0].endSample < total);
    QVERIFY(segments[0].endSample - segments[0].beginSample >= 1600u);
}

// A pause whose probability hovers at the end threshold (well above the 0.01
// noise floor that broke the single-threshold path) must close the segment
// once it reaches the configured silence duration.
void TestVadBackend::pauseAboveNoiseClosesAfterSilence()
{
    const float start = 0.15f;
    const float end = 0.10f;
    const int speechFrames = 1000 * kWindow / (kSamplesPerMs * 1000);
    // Exactly 300ms of below-end frames (10 frames * 32ms = 320ms >= 300ms).
    const int closeFrames = 300 * kWindow / (kSamplesPerMs * 1000) + 1;

    std::string frames(speechFrames, 's');
    frames += std::string(closeFrames, 'e');

    const auto probs = probsFromString(frames, start, end);
    const size_t total = static_cast<size_t>(probs.size()) * kWindow;
    const auto segments = segmentByDualThreshold(
        probs.data(), static_cast<int>(probs.size()), kWindow, total, start, end, 100, 300);

    QCOMPARE(segments.size(), size_t(1));
    QVERIFY(segments[0].endSample < total);
}

// A sub-threshold dip shorter than minSilenceMs must not split the sentence.
void TestVadBackend::briefWobbleDoesNotFragment()
{
    const float start = 0.15f;
    const float end = 0.10f;
    const int speechFrames = 1000 * kWindow / (kSamplesPerMs * 1000);
    // 3 frames (~96ms) below end — below the 300ms gate, so the segment stays
    // open through the dip and remains a single utterance.
    const int wobbleFrames = 3;

    std::string frames(speechFrames, 's');
    frames += std::string(wobbleFrames, 'e');
    frames += std::string(speechFrames, 's');

    const auto probs = probsFromString(frames, start, end);
    const size_t total = static_cast<size_t>(probs.size()) * kWindow;
    const auto segments = segmentByDualThreshold(
        probs.data(), static_cast<int>(probs.size()), kWindow, total, start, end, 100, 300);

    QCOMPARE(segments.size(), size_t(1));
}

// Speech still in progress at the end of the buffer is returned as an open
// segment whose end touches totalSamples so the live segmenter waits.
void TestVadBackend::openSpeechEndsAtTotalSamples()
{
    const float start = 0.15f;
    const float end = 0.10f;
    const int speechFrames = 1000 * kWindow / (kSamplesPerMs * 1000);

    const auto probs = probsFromString(std::string(speechFrames, 's'), start, end);
    const size_t total = static_cast<size_t>(probs.size()) * kWindow;
    const auto segments = segmentByDualThreshold(
        probs.data(), static_cast<int>(probs.size()), kWindow, total, start, end, 100, 300);

    QCOMPARE(segments.size(), size_t(1));
    QCOMPARE(segments[0].endSample, total);
    QCOMPARE(segments[0].beginSample, uint64_t(0));
}

// A closed segment shorter than minSpeechMs is dropped entirely.
void TestVadBackend::tooShortClosedSegmentDropped()
{
    const float start = 0.15f;
    const float end = 0.10f;
    // 2 speech frames (~64ms) then a long pause — below the 100ms minimum.
    const int shortSpeech = 2;
    const int closeFrames = 300 * kWindow / (kSamplesPerMs * 1000) + 1;

    std::string frames(shortSpeech, 's');
    frames += std::string(closeFrames, 'e');

    const auto probs = probsFromString(frames, start, end);
    const size_t total = static_cast<size_t>(probs.size()) * kWindow;
    const auto segments = segmentByDualThreshold(
        probs.data(), static_cast<int>(probs.size()), kWindow, total, start, end, 100, 300);

    QVERIFY(segments.empty());
}

// The decoupled segmenter must not merge across large gaps — that is the
// live segmenter's job via its rolling buffer and overlap policy.
void TestVadBackend::defaultsDoNotMergeAdjacentGaps()
{
    const float start = 0.15f;
    const float end = 0.10f;
    const int speechFrames = 1000 * kWindow / (kSamplesPerMs * 1000);
    const int closeFrames = 300 * kWindow / (kSamplesPerMs * 1000) + 1;
    // A 1s gap between two sentences — far larger than any merge window.
    const int gapFrames = 1000 * kWindow / (kSamplesPerMs * 1000);

    std::string frames(speechFrames, 's');
    frames += std::string(closeFrames, 'e');
    frames += std::string(gapFrames, '.');
    frames += std::string(speechFrames, 's');
    frames += std::string(closeFrames, 'e');

    const auto probs = probsFromString(frames, start, end);
    const size_t total = static_cast<size_t>(probs.size()) * kWindow;
    const auto segments = segmentByDualThreshold(
        probs.data(), static_cast<int>(probs.size()), kWindow, total, start, end, 100, 300);

    QCOMPARE(segments.size(), size_t(2));
}

QTEST_GUILESS_MAIN(TestVadBackend)
#include "test_vad_backend.moc"
