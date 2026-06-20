// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "SegmentAsrWorker.h"

#include <condition_variable>
#include <filesystem>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

using namespace echoflow;
namespace fs = std::filesystem;

namespace {

AudioSegment makeSegment(std::initializer_list<int16_t> samples)
{
    AudioSegment segment;
    segment.sampleRate = 16000;
    segment.samples = samples;
    return segment;
}

struct CallbackResult {
    int sequence = -1;
    std::string text;
};

class FakeAsr : public IAsrEngine {
public:
    explicit FakeAsr(std::vector<std::string> results)
        : results_(std::move(results))
    {
    }

    std::string transcribe(const fs::path& audio) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        paths_.push_back(audio);
        if (results_.empty()) {
            return {};
        }
        std::string result = results_.front();
        results_.erase(results_.begin());
        return result;
    }

    std::vector<fs::path> paths() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return paths_;
    }

private:
    mutable std::mutex mutex_;
    std::vector<std::string> results_;
    std::vector<fs::path> paths_;
};

class BlockingAsr : public IAsrEngine {
public:
    explicit BlockingAsr(std::string result)
        : result_(std::move(result))
    {
    }

    std::string transcribe(const fs::path& audio) override
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            paths_.push_back(audio);
            started_ = true;
        }
        cv_.notify_all();

        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] {
            return released_;
        });
        finished_ = true;
        cv_.notify_all();
        return result_;
    }

    bool waitUntilStarted(std::chrono::steady_clock::duration timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, timeout, [this] {
            return started_;
        });
    }

    void release()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            released_ = true;
        }
        cv_.notify_all();
    }

    bool waitUntilFinished(std::chrono::steady_clock::duration timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, timeout, [this] {
            return finished_;
        });
    }

    std::vector<fs::path> paths() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return paths_;
    }

private:
    std::string result_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool started_ = false;
    bool released_ = false;
    bool finished_ = false;
    std::vector<fs::path> paths_;
};

}  // namespace

class TestSegmentAsrWorker : public QObject {
    Q_OBJECT

private slots:
    void transcribesQueuedSegmentsInOrder();
    void skipsEmptyResults();
    void cancelDropsPendingResults();
    void removesTempWavFilesAfterFinish();
    void finishAndWaitReturnsFalseAndJoinsAfterTimeout();
    void finishAndWaitReturnsPromptlyWhenAsrBlocksPastTimeout();
    void destroysPromptlyAfterTimeoutAndSuppressesLateCallback();
    void cancelAndWaitReturnsPromptlyAfterTimeout();
    void callbackRunsOutsideWorkerLock();
    void cancelWaitsForInFlightCallback();
    void callbackCanCancelWorker();
};

void TestSegmentAsrWorker::transcribesQueuedSegmentsInOrder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    FakeAsr asr({"你好", "世界"});
    std::vector<CallbackResult> callbacks;
    SegmentAsrWorker worker(asr, dir.path().toStdString(), [&](int sequence, const std::string& text) {
        callbacks.push_back({sequence, text});
    });

    worker.start();
    worker.enqueue(makeSegment({1, 2, 3}));
    worker.enqueue(makeSegment({4, 5, 6}));

    QVERIFY(worker.finishAndWait(std::chrono::seconds(1)));
    QCOMPARE(callbacks.size(), size_t(2));
    QCOMPARE(callbacks[0].sequence, 0);
    QCOMPARE(callbacks[0].text, std::string("你好"));
    QCOMPARE(callbacks[1].sequence, 1);
    QCOMPARE(callbacks[1].text, std::string("世界"));
    QCOMPARE(asr.paths().size(), size_t(2));
}

void TestSegmentAsrWorker::skipsEmptyResults()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    FakeAsr asr({"", "世界"});
    std::vector<CallbackResult> callbacks;
    SegmentAsrWorker worker(asr, dir.path().toStdString(), [&](int sequence, const std::string& text) {
        callbacks.push_back({sequence, text});
    });

    worker.start();
    worker.enqueue(makeSegment({1, 2, 3}));
    worker.enqueue(makeSegment({4, 5, 6}));

    QVERIFY(worker.finishAndWait(std::chrono::seconds(1)));
    QCOMPARE(callbacks.size(), size_t(1));
    QCOMPARE(callbacks[0].sequence, 1);
    QCOMPARE(callbacks[0].text, std::string("世界"));
}

void TestSegmentAsrWorker::cancelDropsPendingResults()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BlockingAsr asr("你好");
    std::vector<CallbackResult> callbacks;
    SegmentAsrWorker worker(asr, dir.path().toStdString(), [&](int sequence, const std::string& text) {
        callbacks.push_back({sequence, text});
    });

    worker.start();
    worker.enqueue(makeSegment({1, 2, 3}));
    worker.enqueue(makeSegment({4, 5, 6}));
    QVERIFY(asr.waitUntilStarted(std::chrono::seconds(1)));

    std::promise<void> cancelStarted;
    auto cancelEntered = cancelStarted.get_future();
    auto cancelled = std::async(std::launch::async, [&worker, &cancelStarted] {
        cancelStarted.set_value();
        worker.cancelAndWait();
    });
    cancelEntered.get();
    QVERIFY(cancelled.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready);
    asr.release();
    cancelled.get();

    QVERIFY(callbacks.empty());
    QCOMPARE(asr.paths().size(), size_t(1));
}

void TestSegmentAsrWorker::removesTempWavFilesAfterFinish()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    FakeAsr asr({"你好", "世界"});
    std::vector<CallbackResult> callbacks;
    SegmentAsrWorker worker(asr, dir.path().toStdString(), [&](int sequence, const std::string& text) {
        callbacks.push_back({sequence, text});
    });

    worker.start();
    worker.enqueue(makeSegment({1, 2, 3}));
    worker.enqueue(makeSegment({4, 5, 6}));

    QVERIFY(worker.finishAndWait(std::chrono::seconds(1)));
    for (const auto& path : asr.paths()) {
        QVERIFY(!fs::exists(path));
    }
}

void TestSegmentAsrWorker::finishAndWaitReturnsFalseAndJoinsAfterTimeout()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BlockingAsr asr("你好");
    std::vector<CallbackResult> callbacks;
    SegmentAsrWorker worker(asr, dir.path().toStdString(), [&](int sequence, const std::string& text) {
        callbacks.push_back({sequence, text});
    });

    worker.start();
    worker.enqueue(makeSegment({1, 2, 3}));
    QVERIFY(asr.waitUntilStarted(std::chrono::seconds(1)));

    QVERIFY(!worker.finishAndWait(std::chrono::milliseconds(1)));
    QVERIFY(callbacks.empty());

    asr.release();
    worker.cancelAndWait();

    QVERIFY(callbacks.empty());
}

void TestSegmentAsrWorker::finishAndWaitReturnsPromptlyWhenAsrBlocksPastTimeout()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BlockingAsr asr("你好");
    std::vector<CallbackResult> callbacks;
    SegmentAsrWorker worker(asr, dir.path().toStdString(), [&](int sequence, const std::string& text) {
        callbacks.push_back({sequence, text});
    });

    worker.start();
    worker.enqueue(makeSegment({1, 2, 3}));
    QVERIFY(asr.waitUntilStarted(std::chrono::seconds(1)));

    const auto started = std::chrono::steady_clock::now();
    QVERIFY(!worker.finishAndWait(std::chrono::milliseconds(50)));
    const auto elapsed = std::chrono::steady_clock::now() - started;

    QVERIFY(elapsed < std::chrono::milliseconds(500));
    QVERIFY(callbacks.empty());

    asr.release();
    worker.cancelAndWait();
    QVERIFY(callbacks.empty());
}

void TestSegmentAsrWorker::destroysPromptlyAfterTimeoutAndSuppressesLateCallback()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BlockingAsr asr("你好");
    std::vector<CallbackResult> callbacks;

    auto destroyed = std::async(std::launch::async, [&] {
        {
            SegmentAsrWorker worker(asr, dir.path().toStdString(), [&](int sequence, const std::string& text) {
                callbacks.push_back({sequence, text});
            });
            worker.start();
            worker.enqueue(makeSegment({1, 2, 3}));
            if (!asr.waitUntilStarted(std::chrono::seconds(1))) {
                return false;
            }
            if (worker.finishAndWait(std::chrono::milliseconds(50))) {
                return false;
            }
        }
        return true;
    });

    const bool returnedPromptly = destroyed.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready;
    if (!returnedPromptly) {
        asr.release();
        destroyed.wait();
    }
    QVERIFY(returnedPromptly);
    QVERIFY(destroyed.get());
    QVERIFY(callbacks.empty());

    asr.release();
    QVERIFY(asr.waitUntilFinished(std::chrono::seconds(1)));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    QVERIFY(callbacks.empty());
}

void TestSegmentAsrWorker::cancelAndWaitReturnsPromptlyAfterTimeout()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BlockingAsr asr("你好");
    std::vector<CallbackResult> callbacks;
    SegmentAsrWorker worker(asr, dir.path().toStdString(), [&](int sequence, const std::string& text) {
        callbacks.push_back({sequence, text});
    });

    worker.start();
    worker.enqueue(makeSegment({1, 2, 3}));
    QVERIFY(asr.waitUntilStarted(std::chrono::seconds(1)));
    QVERIFY(!worker.finishAndWait(std::chrono::milliseconds(50)));

    const auto started = std::chrono::steady_clock::now();
    worker.cancelAndWait();
    const auto elapsed = std::chrono::steady_clock::now() - started;

    QVERIFY(elapsed < std::chrono::milliseconds(500));
    QVERIFY(callbacks.empty());

    asr.release();
    QVERIFY(asr.waitUntilFinished(std::chrono::seconds(1)));
    QVERIFY(callbacks.empty());
}

void TestSegmentAsrWorker::callbackRunsOutsideWorkerLock()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    FakeAsr asr({"你好"});
    SegmentAsrWorker* workerPtr = nullptr;
    bool callbackReturned = false;
    SegmentAsrWorker worker(asr, dir.path().toStdString(), [&](int, const std::string&) {
        workerPtr->enqueue(makeSegment({4, 5, 6}));
        callbackReturned = true;
    });
    workerPtr = &worker;

    worker.start();
    worker.enqueue(makeSegment({1, 2, 3}));

    auto finished = std::async(std::launch::async, [&worker] {
        return worker.finishAndWait(std::chrono::seconds(1));
    });

    QVERIFY(finished.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready);
    QVERIFY(finished.get());
    QVERIFY(callbackReturned);
    QCOMPARE(asr.paths().size(), size_t(1));
}

void TestSegmentAsrWorker::cancelWaitsForInFlightCallback()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    FakeAsr asr({"你好"});
    std::mutex mutex;
    std::condition_variable cv;
    bool callbackStarted = false;
    bool releaseCallback = false;
    SegmentAsrWorker worker(asr, dir.path().toStdString(), [&](int, const std::string&) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            callbackStarted = true;
        }
        cv.notify_all();
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] {
            return releaseCallback;
        });
    });

    worker.start();
    worker.enqueue(makeSegment({1, 2, 3}));
    {
        std::unique_lock<std::mutex> lock(mutex);
        QVERIFY(cv.wait_for(lock, std::chrono::seconds(1), [&] {
            return callbackStarted;
        }));
    }

    auto cancelled = std::async(std::launch::async, [&worker] {
        worker.cancelAndWait();
    });
    QVERIFY(cancelled.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready);

    {
        std::lock_guard<std::mutex> lock(mutex);
        releaseCallback = true;
    }
    cv.notify_all();
    QVERIFY(cancelled.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    cancelled.get();
}

void TestSegmentAsrWorker::callbackCanCancelWorker()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    FakeAsr asr({"你好"});
    SegmentAsrWorker* workerPtr = nullptr;
    bool callbackReturned = false;
    SegmentAsrWorker worker(asr, dir.path().toStdString(), [&](int, const std::string&) {
        workerPtr->cancelAndWait();
        callbackReturned = true;
    });
    workerPtr = &worker;

    worker.start();
    worker.enqueue(makeSegment({1, 2, 3}));

    auto finished = std::async(std::launch::async, [&worker] {
        return worker.finishAndWait(std::chrono::seconds(1));
    });

    QVERIFY(finished.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready);
    QVERIFY(finished.get());
    QVERIFY(callbackReturned);
}

QTEST_MAIN(TestSegmentAsrWorker)
#include "test_segment_asr_worker.moc"
