// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SegmentAsrWorker.h"
#include "LiveSegmenter.h"
#include "LiveSegmentCoordinator.h"
#include "LiveDebugRecorder.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace echoflow;

namespace {

class OneShotSegmenter : public ILiveSegmenter {
public:
    std::vector<AudioSegment> append(const int16_t* samples, size_t count) override
    {
        AudioSegment segment;
        segment.beginSample = 0;
        segment.endSample = count;
        segment.samples.assign(samples, samples + count);
        return {std::move(segment)};
    }

    std::optional<AudioSegment> flush() override { return std::nullopt; }
    void reset() override { resetCalled = true; }
    std::string name() const override { return "fake"; }
    std::string diagnosticSummary() const override { return "calls=1"; }

    bool resetCalled = false;
};

struct BlockingState {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false;
    bool released = false;
};

class BlockingSegmenter : public ILiveSegmenter {
public:
    explicit BlockingSegmenter(std::shared_ptr<BlockingState> state)
        : state_(std::move(state))
    {
    }

    std::vector<AudioSegment> append(const int16_t*, size_t) override
    {
        std::unique_lock<std::mutex> lock(state_->mutex);
        state_->entered = true;
        state_->changed.notify_all();
        state_->changed.wait(lock, [this] { return state_->released; });
        return {};
    }

    std::optional<AudioSegment> flush() override { return std::nullopt; }
    void reset() override {}
    std::string name() const override { return "blocking"; }
    std::string diagnosticSummary() const override { return "blocked=false"; }

private:
    std::shared_ptr<BlockingState> state_;
};

class ThrowingSegmenter : public ILiveSegmenter {
public:
    explicit ThrowingSegmenter(std::shared_ptr<BlockingState> state)
        : state_(std::move(state))
    {
    }

    std::vector<AudioSegment> append(const int16_t*, size_t) override
    {
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            state_->entered = true;
            state_->changed.notify_all();
        }
        throw std::runtime_error("VAD inference failed");
    }

    std::optional<AudioSegment> flush() override { return std::nullopt; }
    void reset() override {}
    std::string name() const override { return "throwing"; }
    std::string diagnosticSummary() const override { return "failed=true"; }

private:
    std::shared_ptr<BlockingState> state_;
};

}  // namespace

class TestSegmentAsrWorker : public QObject {
    Q_OBJECT

private slots:
    void slowTranscriberPreservesOrderAndEverySegment();
    void sustainedInputIsPreservedWhileTranscriptionIsSlow();
    void completedSegmentsPublishAccumulatedTextBeforeFinish();
    void debugRecorderWritesExactPcmAndMetrics();
    void coordinatorAcceptsInjectedLiveSegmenter();
    void slowSegmentationDoesNotBlockCapture();
    void segmentationBackpressureFailsClosed();
    void segmentationFailurePropagatesAndStopsCapture();
};

void TestSegmentAsrWorker::coordinatorAcceptsInjectedLiveSegmenter()
{
    auto segmenter = std::make_unique<OneShotSegmenter>();
    LiveSegmentCoordinator coordinator(
        std::move(segmenter),
        [](const AudioSegment& segment) { return std::to_string(segment.sampleCount()); });
    coordinator.start();

    const std::vector<int16_t> pcm(1600, 7);
    QVERIFY(coordinator.append(pcm.data(), pcm.size()));
    QCOMPARE(coordinator.finish(), std::vector<std::string>({"1600"}));
}

void TestSegmentAsrWorker::slowSegmentationDoesNotBlockCapture()
{
    auto state = std::make_shared<BlockingState>();
    LiveSegmentCoordinator coordinator(
        std::make_unique<BlockingSegmenter>(state),
        [](const AudioSegment&) { return std::string(); });
    coordinator.start();

    const std::vector<int16_t> pcm(1600, 7);
    auto appendResult = std::async(std::launch::async, [&] {
        return coordinator.append(pcm.data(), pcm.size());
    });
    const bool returnedPromptly =
        appendResult.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->released = true;
        state->changed.notify_all();
    }

    QVERIFY(returnedPromptly);
    QVERIFY(appendResult.get());
    coordinator.finish();
}

void TestSegmentAsrWorker::segmentationBackpressureFailsClosed()
{
    auto state = std::make_shared<BlockingState>();
    LiveSegmentCoordinator coordinator(
        std::make_unique<BlockingSegmenter>(state),
        [](const AudioSegment&) { return std::string(); }, {}, 1600);
    coordinator.start();

    const std::vector<int16_t> pcm(1600, 7);
    QVERIFY(coordinator.append(pcm.data(), pcm.size()));
    {
        std::unique_lock<std::mutex> lock(state->mutex);
        QVERIFY(state->changed.wait_for(lock, std::chrono::seconds(1), [&] {
            return state->entered;
        }));
    }
    QVERIFY(coordinator.append(pcm.data(), pcm.size()));
    QVERIFY(!coordinator.append(pcm.data(), pcm.size()));

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->released = true;
        state->changed.notify_all();
    }
    coordinator.cancel();
    QVERIFY(coordinator.metrics().audioDropped);
}

void TestSegmentAsrWorker::segmentationFailurePropagatesAndStopsCapture()
{
    auto state = std::make_shared<BlockingState>();
    LiveSegmentCoordinator coordinator(
        std::make_unique<ThrowingSegmenter>(state),
        [](const AudioSegment&) { return std::string(); });
    coordinator.start();

    const std::vector<int16_t> pcm(1600, 7);
    QVERIFY(coordinator.append(pcm.data(), pcm.size()));
    {
        std::unique_lock<std::mutex> lock(state->mutex);
        QVERIFY(state->changed.wait_for(lock, std::chrono::seconds(1), [&] {
            return state->entered;
        }));
    }
    QVERIFY_EXCEPTION_THROWN(coordinator.finish(), std::runtime_error);
    QVERIFY(!coordinator.append(pcm.data(), pcm.size()));
    QVERIFY(coordinator.metrics().audioDropped);
}

void TestSegmentAsrWorker::slowTranscriberPreservesOrderAndEverySegment()
{
    SegmentAsrWorker worker([](const AudioSegment& segment) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return std::to_string(segment.samples.front());
    });

    worker.start();
    for (int i = 0; i < 12; ++i) {
        AudioSegment segment;
        segment.samples = {static_cast<int16_t>(i)};
        QVERIFY(worker.enqueue(std::move(segment)));
    }

    const std::vector<std::string> expected = {
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11"};
    QCOMPARE(worker.finish(), expected);
    QCOMPARE(worker.enqueuedCount(), size_t(12));
    QCOMPARE(worker.completedCount(), size_t(12));
    QVERIFY(worker.highWaterMark() > 1);
}

void TestSegmentAsrWorker::debugRecorderWritesExactPcmAndMetrics()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    LiveDebugRecorder recorder(dir.path().toStdString());
    recorder.start();
    std::vector<int16_t> samples(32000, int16_t(1234));
    recorder.append(samples.data(), samples.size());
    LivePipelineMetrics metrics;
    metrics.inputSamples = samples.size();
    metrics.segmentSamples = samples.size();
    metrics.enqueuedSegments = 2;
    metrics.asrQueueHighWaterMark = 1;
    recorder.finish(metrics, {"第一段", "第二段"});

    QFile wav(QString::fromStdString(recorder.wavPath().string()));
    QVERIFY(wav.open(QIODevice::ReadOnly));
    QCOMPARE(wav.size(), qint64(44 + samples.size() * sizeof(int16_t)));
    const QByteArray wavBytes = wav.readAll();
    QCOMPARE(wavBytes.mid(0, 4), QByteArray("RIFF"));
    QCOMPARE(wavBytes.mid(8, 4), QByteArray("WAVE"));
    QCOMPARE(wavBytes.mid(36, 4), QByteArray("data"));

    QFile metadata(QString::fromStdString(recorder.metadataPath().string()));
    QVERIFY(metadata.open(QIODevice::ReadOnly));
    const QByteArray json = metadata.readAll();
    QVERIFY(json.contains("\"captured_samples\":32000"));
    QVERIFY(json.contains("\"segment_samples\":32000"));
    QVERIFY(json.contains("\"audio_dropped\":false"));
    QVERIFY(json.contains("第一段 第二段"));
}

void TestSegmentAsrWorker::completedSegmentsPublishAccumulatedTextBeforeFinish()
{
    AudioSegmenterConfig config;
    config.maxSegmentMs = 200;
    config.minSegmentMs = 100;
    std::mutex mutex;
    std::condition_variable updated;
    std::vector<std::string> latest;
    LiveSegmentCoordinator coordinator(
        config,
        [](const AudioSegment&) { return std::string("一句"); },
        [&](const std::vector<std::string>& results) {
            std::lock_guard<std::mutex> lock(mutex);
            latest = results;
            updated.notify_all();
        });
    coordinator.start();

    std::vector<int16_t> speech(3200);
    for (size_t i = 0; i < speech.size(); ++i) {
        speech[i] = (i % 2 == 0) ? int16_t(3000) : int16_t(-3000);
    }
    QVERIFY(coordinator.append(speech.data(), speech.size()));

    std::unique_lock<std::mutex> lock(mutex);
    QVERIFY(updated.wait_for(lock, std::chrono::seconds(1), [&] { return !latest.empty(); }));
    QCOMPARE(latest, std::vector<std::string>({"一句"}));
    lock.unlock();
    coordinator.finish();
}

void TestSegmentAsrWorker::sustainedInputIsPreservedWhileTranscriptionIsSlow()
{
    AudioSegmenterConfig config;
    config.maxSegmentMs = 1000;
    config.minSegmentMs = 200;
    config.forceOverlapMs = 0;
    LiveSegmentCoordinator coordinator(config, [](const AudioSegment& segment) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        return std::to_string(segment.sampleCount());
    }, {}, 40 * 16000);
    coordinator.start();

    std::vector<int16_t> block(16000);
    for (size_t i = 0; i < block.size(); ++i) {
        block[i] = (i % 2 == 0) ? int16_t(3000) : int16_t(-3000);
    }
    for (int second = 0; second < 40; ++second) {
        QVERIFY(coordinator.append(block.data(), block.size()));
    }

    const std::vector<std::string> results = coordinator.finish();
    const LivePipelineMetrics metrics = coordinator.metrics();
    QCOMPARE(metrics.inputSamples, uint64_t(40 * 16000));
    QCOMPARE(metrics.segmentSamples, metrics.inputSamples);
    QCOMPARE(metrics.enqueuedSegments, size_t(40));
    QCOMPARE(results.size(), size_t(40));
    QVERIFY(metrics.asrQueueHighWaterMark > 1);
    QVERIFY(!metrics.audioDropped);
}

QTEST_GUILESS_MAIN(TestSegmentAsrWorker)
#include "test_segment_asr_worker.moc"
