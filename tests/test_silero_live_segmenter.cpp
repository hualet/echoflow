// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SileroLiveSegmenter.h"
#include "VadBackend.h"

#include <QTest>

#include <algorithm>
#include <memory>
#include <vector>

using namespace echoflow;

namespace {

class MarkerVadBackend final : public IVadBackend {
public:
    std::vector<SpeechInterval> detect(const int16_t* pcm, size_t count) override
    {
        std::vector<SpeechInterval> out;
        size_t i = 0;
        while (i < count) {
            while (i < count && pcm[i] == 0) ++i;
            const size_t begin = i;
            while (i < count && pcm[i] != 0) ++i;
            if (i > begin) out.push_back({begin, i, 1.0f});
        }
        return out;
    }

    std::string name() const override { return "marker"; }
};

class MinimumRunVadBackend final : public IVadBackend {
public:
    explicit MinimumRunVadBackend(size_t minimumRunSamples)
        : minimumRunSamples_(minimumRunSamples)
    {
    }

    std::vector<SpeechInterval> detect(const int16_t* pcm, size_t count) override
    {
        ++calls;
        size_t runBegin = count;
        for (size_t i = 0; i < count; ++i) {
            if (pcm[i] != 0 && runBegin == count) {
                runBegin = i;
            } else if (pcm[i] == 0 && runBegin != count) {
                if (i - runBegin >= minimumRunSamples_) {
                    return {{runBegin, i, 1.0f}};
                }
                runBegin = count;
            }
        }
        if (runBegin != count && count - runBegin >= minimumRunSamples_) {
            return {{runBegin, count, 1.0f}};
        }
        return {};
    }

    std::string name() const override { return "minimum-run"; }

    size_t calls = 0;

private:
    size_t minimumRunSamples_;
};

class ConfirmedSilenceVadBackend final : public IVadBackend {
public:
    std::vector<SpeechInterval> detect(const int16_t*, size_t count) override
    {
        constexpr size_t kSpeechBegin = 3200;
        constexpr size_t kPaddedSpeechEnd = 16480;
        constexpr size_t kEndpointConfirmedAt = 20800;

        if (count <= kSpeechBegin) return {};
        if (count < kEndpointConfirmedAt) {
            return {{kSpeechBegin, count, 1.0f}};
        }
        return {{kSpeechBegin, kPaddedSpeechEnd, 1.0f}};
    }

    std::string name() const override { return "confirmed-silence"; }
};

std::vector<int16_t> pcm(int milliseconds, int16_t value)
{
    return std::vector<int16_t>(static_cast<size_t>(milliseconds * 16), value);
}

void appendPcm(std::vector<int16_t>& target, const std::vector<int16_t>& source)
{
    target.insert(target.end(), source.begin(), source.end());
}

std::unique_ptr<SileroLiveSegmenter> makeSegmenter()
{
    return std::make_unique<SileroLiveSegmenter>(
        SileroLiveSegmenterConfig{}, std::make_unique<MarkerVadBackend>());
}

}  // namespace

class TestSileroLiveSegmenter : public QObject {
    Q_OBJECT

private slots:
    void naturalPauseEmitsBeforeMaximum();
    void unfinishedIntervalWaitsForBackendClosure();
    void backendConfirmedSilenceEmitsImmediately();
    void flushRetainsShortUtterance();
    void flushDiscardsNonSpeech();
    void continuousSpeechUsesForcedOverlap();
    void repeatedAnalysisDoesNotDuplicateNaturalSegment();
    void resetDiscardsPendingSpeech();
    void retainsEnoughHistoryForConfiguredMinimumSpeech();
    void preservesAnalysisCadenceRemainder();
};

void TestSileroLiveSegmenter::naturalPauseEmitsBeforeMaximum()
{
    auto segmenter = makeSegmenter();
    std::vector<int16_t> input = pcm(200, 0);
    appendPcm(input, pcm(800, 1200));
    appendPcm(input, pcm(400, 0));

    const auto segments = segmenter->append(input.data(), input.size());

    QCOMPARE(segments.size(), size_t(1));
    QCOMPARE(segments[0].beginSample, uint64_t(0));
    QCOMPARE(segments[0].endSample, uint64_t(19200));
    QCOMPARE(segments[0].sampleCount(), size_t(19200));
    QCOMPARE(QString::fromStdString(segmenter->name()), QStringLiteral("silero"));
    const QString diagnostics = QString::fromStdString(segmenter->diagnosticSummary());
    QVERIFY(diagnostics.contains("analyses="));
    QVERIFY(diagnostics.contains("analysis_ms="));
}

void TestSileroLiveSegmenter::unfinishedIntervalWaitsForBackendClosure()
{
    SileroLiveSegmenter segmenter(
        SileroLiveSegmenterConfig{},
        std::make_unique<ConfirmedSilenceVadBackend>());
    std::vector<int16_t> input = pcm(200, 0);
    appendPcm(input, pcm(800, 1200));
    appendPcm(input, pcm(200, 0));

    QCOMPARE(segmenter.append(input.data(), input.size()).size(), size_t(0));
    QVERIFY(segmenter.flush().has_value());
}

void TestSileroLiveSegmenter::backendConfirmedSilenceEmitsImmediately()
{
    SileroLiveSegmenter segmenter(
        SileroLiveSegmenterConfig{},
        std::make_unique<ConfirmedSilenceVadBackend>());
    std::vector<int16_t> input = pcm(200, 0);
    appendPcm(input, pcm(800, 1200));
    appendPcm(input, pcm(300, 0));

    const auto segments = segmenter.append(input.data(), input.size());

    QCOMPARE(segments.size(), size_t(1));
    QCOMPARE(segments[0].beginSample, uint64_t(0));
    QCOMPARE(segments[0].endSample, uint64_t(19680));
}

void TestSileroLiveSegmenter::flushRetainsShortUtterance()
{
    auto segmenter = makeSegmenter();
    std::vector<int16_t> input = pcm(200, 0);
    appendPcm(input, pcm(150, 1200));
    QCOMPARE(segmenter->append(input.data(), input.size()).size(), size_t(0));

    const auto tail = segmenter->flush();

    QVERIFY(tail.has_value());
    QCOMPARE(tail->beginSample, uint64_t(0));
    QCOMPARE(tail->endSample, uint64_t(5600));
}

void TestSileroLiveSegmenter::flushDiscardsNonSpeech()
{
    auto segmenter = makeSegmenter();
    const auto silence = pcm(1000, 0);
    QCOMPARE(segmenter->append(silence.data(), silence.size()).size(), size_t(0));
    QVERIFY(!segmenter->flush().has_value());
}

void TestSileroLiveSegmenter::continuousSpeechUsesForcedOverlap()
{
    auto segmenter = makeSegmenter();
    const auto speech = pcm(8200, 1200);

    const auto segments = segmenter->append(speech.data(), speech.size());

    QCOMPARE(segments.size(), size_t(1));
    QCOMPARE(segments[0].sampleCount(), size_t(128000));
    const auto tail = segmenter->flush();
    QVERIFY(tail.has_value());
    QCOMPARE(tail->beginSample, uint64_t(120000));
    QCOMPARE(tail->endSample, uint64_t(131200));
    QCOMPARE(tail->sampleCount(), size_t(11200));
}

void TestSileroLiveSegmenter::repeatedAnalysisDoesNotDuplicateNaturalSegment()
{
    auto segmenter = makeSegmenter();
    std::vector<int16_t> input = pcm(200, 0);
    appendPcm(input, pcm(800, 1200));
    appendPcm(input, pcm(400, 0));

    size_t emitted = 0;
    for (size_t offset = 0; offset < input.size(); offset += 1600) {
        const size_t count = std::min(size_t(1600), input.size() - offset);
        emitted += segmenter->append(input.data() + offset, count).size();
    }

    QCOMPARE(emitted, size_t(1));
    QVERIFY(!segmenter->flush().has_value());
}

void TestSileroLiveSegmenter::resetDiscardsPendingSpeech()
{
    auto segmenter = makeSegmenter();
    const auto speech = pcm(500, 1200);
    segmenter->append(speech.data(), speech.size());
    segmenter->reset();
    const auto silence = pcm(500, 0);
    segmenter->append(silence.data(), silence.size());

    QVERIFY(!segmenter->flush().has_value());
}

void TestSileroLiveSegmenter::retainsEnoughHistoryForConfiguredMinimumSpeech()
{
    SileroLiveSegmenterConfig config;
    config.minSpeechMs = 500;
    auto segmenter = std::make_unique<SileroLiveSegmenter>(
        config, std::make_unique<MinimumRunVadBackend>(8000));

    const auto speechChunk = pcm(100, 1200);
    for (int i = 0; i < 5; ++i) {
        QCOMPARE(segmenter->append(speechChunk.data(), speechChunk.size()).size(), size_t(0));
    }
    const auto silence = pcm(400, 0);
    const auto segments = segmenter->append(silence.data(), silence.size());

    QCOMPARE(segments.size(), size_t(1));
    QCOMPARE(segments[0].sampleCount(), size_t(11200));
}

void TestSileroLiveSegmenter::preservesAnalysisCadenceRemainder()
{
    SileroLiveSegmenterConfig config;
    auto backend = std::make_unique<MinimumRunVadBackend>(16000);
    MinimumRunVadBackend* observed = backend.get();
    SileroLiveSegmenter segmenter(config, std::move(backend));

    const auto first = pcm(150, 0);
    segmenter.append(first.data(), first.size());
    QCOMPARE(observed->calls, size_t(1));

    const auto second = pcm(50, 0);
    segmenter.append(second.data(), second.size());
    QCOMPARE(observed->calls, size_t(2));
}

QTEST_GUILESS_MAIN(TestSileroLiveSegmenter)
#include "test_silero_live_segmenter.moc"
