// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "VoiceSession.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace echoflow;
using namespace std::chrono_literals;

struct FakeRecorder : IRecorder {
    mutable std::mutex mutex;
    int starts_ = 0;
    int stops_ = 0;
    std::filesystem::path path = "/tmp/echoflow-test.wav";

    void start() override
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++starts_;
    }
    std::filesystem::path stop() override
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++stops_;
        return path;
    }

    int starts() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return starts_;
    }

    int stops() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return stops_;
    }
};

struct FakeAsr : IAsrEngine {
    explicit FakeAsr(std::string r = "hello")
        : result(std::move(r))
    {
    }

    mutable std::mutex mutex;
    std::string result;
    bool throwOnTranscribe = false;
    std::vector<std::filesystem::path> audioPaths_;

    std::string transcribe(const std::filesystem::path& audio) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        audioPaths_.push_back(audio);
        if (throwOnTranscribe) {
            throw std::runtime_error("asr unavailable");
        }
        return result;
    }

    std::vector<std::filesystem::path> audioPaths() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return audioPaths_;
    }
};

struct FakeLivePipeline : ILiveVoicePipeline {
    mutable std::mutex mutex;
    int starts_ = 0;
    int finishes_ = 0;
    int cancels_ = 0;
    std::string result = "hello";
    bool throwOnStart = false;
    bool throwOnFinish = false;
    bool throwOnCancel = false;
    std::function<void(const std::string&)> partialTextCallback;

    void start() override
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++starts_;
        if (throwOnStart) {
            throw std::runtime_error("live start unavailable");
        }
    }

    std::string finish() override
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++finishes_;
        if (throwOnFinish) {
            throw std::runtime_error("live finish unavailable");
        }
        return result;
    }

    void cancel() override
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++cancels_;
        if (throwOnCancel) {
            throw std::runtime_error("live cancel unavailable");
        }
    }

    void setPartialTextCallback(std::function<void(const std::string&)> callback) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        partialTextCallback = std::move(callback);
    }

    int starts() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return starts_;
    }

    int finishes() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return finishes_;
    }

    int cancels() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return cancels_;
    }

    std::function<void(const std::string&)> callback() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return partialTextCallback;
    }
};

struct BlockingLivePipeline : FakeLivePipeline {
    std::condition_variable condition;
    bool finishEntered_ = false;
    bool finishReleased_ = false;
    bool finishExited_ = false;

    std::string finish() override
    {
        std::unique_lock<std::mutex> lock(mutex);
        ++finishes_;
        finishEntered_ = true;
        condition.notify_all();
        condition.wait(lock, [this] { return finishReleased_; });
        finishExited_ = true;
        condition.notify_all();
        if (throwOnFinish) {
            throw std::runtime_error("live finish unavailable");
        }
        return result;
    }

    bool waitForFinishEntered(std::chrono::milliseconds timeout = 1000ms)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return condition.wait_for(lock, timeout, [this] { return finishEntered_; });
    }

    void releaseFinish()
    {
        std::lock_guard<std::mutex> lock(mutex);
        finishReleased_ = true;
        condition.notify_all();
    }

    bool waitForFinishExited(std::chrono::milliseconds timeout = 1000ms)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return condition.wait_for(lock, timeout, [this] { return finishExited_; });
    }
};

struct FakeCommitter : ICommitter {
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::vector<std::string> texts_;
    std::pair<bool, std::string> ret = { true, "OK" };

    std::pair<bool, std::string> commitText(const std::string& text) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        texts_.push_back(text);
        condition.notify_all();
        return ret;
    }

    std::vector<std::string> texts() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return texts_;
    }
};

struct FakeUi : IUiNotifier {
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::vector<std::string> messages_;

    void send(const std::string& message) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        messages_.push_back(message);
        condition.notify_all();
    }

    std::vector<std::string> messages() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return messages_;
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex);
        messages_.clear();
    }

    bool waitForMessage(const std::string& message, std::chrono::milliseconds timeout = 1000ms)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return condition.wait_for(lock, timeout, [this, &message] {
            return std::find(messages_.begin(), messages_.end(), message) != messages_.end();
        });
    }
};

struct BlockingCancellationUi : IUiNotifier {
    mutable std::mutex messagesMutex;
    std::condition_variable messagesCondition;
    std::vector<std::string> messages_;
    std::mutex cancellationMutex;
    std::condition_variable cancellationCondition;
    bool cancellationEntered_ = false;
    bool cancellationReleased_ = false;

    void send(const std::string& message) override
    {
        if (message == "CANCELLING") {
            std::unique_lock<std::mutex> lock(cancellationMutex);
            cancellationEntered_ = true;
            cancellationCondition.notify_all();
            cancellationCondition.wait(lock, [this] { return cancellationReleased_; });
        }

        std::lock_guard<std::mutex> lock(messagesMutex);
        messages_.push_back(message);
        messagesCondition.notify_all();
    }

    bool waitForCancellationEntered(std::chrono::milliseconds timeout = 1000ms)
    {
        std::unique_lock<std::mutex> lock(cancellationMutex);
        return cancellationCondition.wait_for(
            lock, timeout, [this] { return cancellationEntered_; });
    }

    void releaseCancellation()
    {
        std::lock_guard<std::mutex> lock(cancellationMutex);
        cancellationReleased_ = true;
        cancellationCondition.notify_all();
    }

    bool waitForMessage(const std::string& message, std::chrono::milliseconds timeout = 1000ms)
    {
        std::unique_lock<std::mutex> lock(messagesMutex);
        return messagesCondition.wait_for(lock, timeout, [this, &message] {
            return std::find(messages_.begin(), messages_.end(), message) != messages_.end();
        });
    }

    std::vector<std::string> messages() const
    {
        std::lock_guard<std::mutex> lock(messagesMutex);
        return messages_;
    }
};

class TestVoiceSession : public QObject {
    Q_OBJECT

private slots:
    void ctrlStartsRecording();
    void focusIsNowHarmless();
    void typedIsNowHarmless();
    void secondCtrlStopsTranscribesAndCommits();
    void transcribeExceptionReturnsToIdle();
    void tooShortAudioIsCancelled();
    void cancelDiscardsRecording();
    void blurWhileRecordingCancelsAndDiscards();
    void liveCtrlStartsPipeline();
    void liveSecondCtrlFinishesAndCommits();
    void liveEmptyResultDoesNotCommit();
    void liveFinishExceptionReturnsToIdle();
    void liveCancelDiscardsRecording();
    void liveCancelExceptionStillReturnsToIdleAndHidesTooltip();
    void liveBlurCancelsAndDiscards();
    void livePartialTextUpdatesTooltip();
    void livePreviewDoesNotAffectCommittedText();
    void liveStartExceptionReturnsToIdle();
    void stopReturnsWhileLiveFinishIsBlocked();
    void cancelDuringTranscribingDiscardsFinishedText();
    void ctrlDuringCancellingDoesNotRestartPipeline();
    void cancellingNotificationPrecedesFinalIdle();
    void statusReportsEveryPhaseWithoutMutation();
    void blurDuringTranscribingDiscardsFinishedText();
    void destructorWaitsForFinalization();
    void latePartialAfterStopIsIgnored();
    void unknownCommandReturnsError();
};

static VoiceSession makeSession(
    FakeRecorder& recorder, FakeAsr& asr, FakeCommitter& committer, FakeUi& ui)
{
    return VoiceSession(Config::defaultConfig(), recorder, asr, committer, ui);
}

static VoiceSession makeLiveSession(
    FakeLivePipeline& pipeline, FakeCommitter& committer, FakeUi& ui)
{
    return VoiceSession(Config::defaultConfig(), pipeline, committer, ui);
}

void TestVoiceSession::ctrlStartsRecording()
{
    FakeRecorder recorder;
    FakeAsr asr;
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeSession(recorder, asr, committer, ui);

    QCOMPARE(
        QString::fromStdString(session.handleCommand("CTRL_DOWN")), QStringLiteral("RECORDING"));
    QCOMPARE(recorder.starts(), 1);
    QCOMPARE(session.state(), SessionState::Recording);
    QCOMPARE(QString::fromStdString(ui.messages().back()), QStringLiteral("RECORDING"));
}

void TestVoiceSession::focusIsNowHarmless()
{
    // The capsule no longer tracks input focus, so FOCUS must not emit any UI
    // message. A subsequent CTRL_DOWN still starts recording normally.
    FakeRecorder recorder;
    FakeAsr asr;
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeSession(recorder, asr, committer, ui);

    QCOMPARE(QString::fromStdString(session.handleCommand("FOCUS")), QStringLiteral("IGNORED"));
    QCOMPARE(QString::fromStdString(session.handleCommand("FOCUS 120 240 2 18")),
        QStringLiteral("IGNORED"));
    QVERIFY(ui.messages().empty());

    QCOMPARE(
        QString::fromStdString(session.handleCommand("CTRL_DOWN")), QStringLiteral("RECORDING"));
}

void TestVoiceSession::typedIsNowHarmless()
{
    // TYPED no longer hides the capsule; it is accepted and ignored in every
    // state, never disturbing an active recording.
    FakeRecorder recorder;
    FakeAsr asr;
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeSession(recorder, asr, committer, ui);

    QCOMPARE(QString::fromStdString(session.handleCommand("TYPED")), QStringLiteral("IGNORED"));
    QVERIFY(ui.messages().empty());

    session.handleCommand("CTRL_DOWN");
    ui.clear();
    QCOMPARE(QString::fromStdString(session.handleCommand("TYPED")), QStringLiteral("IGNORED"));
    QVERIFY(ui.messages().empty());
    QCOMPARE(session.state(), SessionState::Recording);
}

void TestVoiceSession::secondCtrlStopsTranscribesAndCommits()
{
    FakeRecorder recorder;
    FakeAsr asr("离线语音输入");
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeSession(recorder, asr, committer, ui);

    session.handleCommand("CTRL_DOWN");

    QCOMPARE(
        QString::fromStdString(session.handleCommand("CTRL_DOWN")), QStringLiteral("TRANSCRIBING"));
    QTRY_COMPARE(session.state(), SessionState::Idle);
    QVERIFY(ui.waitForMessage("IDLE"));
    QCOMPARE(recorder.stops(), 1);
    const auto audioPaths = asr.audioPaths();
    QCOMPARE(audioPaths.size(), size_t(1));
    QCOMPARE(audioPaths.front(), recorder.path);
    const auto texts = committer.texts();
    QCOMPARE(texts.size(), size_t(1));
    QCOMPARE(QString::fromStdString(texts.front()), QStringLiteral("离线语音输入"));
    const auto messages = ui.messages();
    QCOMPARE(
        QString::fromStdString(messages.at(messages.size() - 2)), QStringLiteral("TRANSCRIBING"));
    QCOMPARE(QString::fromStdString(messages.back()), QStringLiteral("IDLE"));
}

void TestVoiceSession::transcribeExceptionReturnsToIdle()
{
    FakeRecorder recorder;
    FakeAsr asr;
    asr.throwOnTranscribe = true;
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeSession(recorder, asr, committer, ui);

    session.handleCommand("CTRL_DOWN");

    QCOMPARE(
        QString::fromStdString(session.handleCommand("CTRL_DOWN")), QStringLiteral("TRANSCRIBING"));
    QTRY_COMPARE(session.state(), SessionState::Idle);
    QVERIFY(ui.waitForMessage("IDLE"));
    QVERIFY(committer.texts().empty());
    const auto messages = ui.messages();
    QCOMPARE(
        QString::fromStdString(messages.at(messages.size() - 2)), QStringLiteral("TRANSCRIBING"));
    QCOMPARE(QString::fromStdString(messages.back()), QStringLiteral("IDLE"));
}

void TestVoiceSession::tooShortAudioIsCancelled()
{
    FakeRecorder recorder;
    recorder.path.clear();
    FakeAsr asr;
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeSession(recorder, asr, committer, ui);

    session.handleCommand("CTRL_DOWN");
    QCOMPARE(
        QString::fromStdString(session.handleCommand("CTRL_DOWN")), QStringLiteral("TRANSCRIBING"));
    QTRY_COMPARE(session.state(), SessionState::Idle);
    QVERIFY(ui.waitForMessage("IDLE"));
    QCOMPARE(recorder.stops(), 1);
    QVERIFY(committer.texts().empty());
}

void TestVoiceSession::cancelDiscardsRecording()
{
    // The capsule's X button sends CANCEL: stop the recorder, discard the
    // audio, never transcribe/commit, and emit IDLE so the UI hides.
    FakeRecorder recorder;
    FakeAsr asr;
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeSession(recorder, asr, committer, ui);

    session.handleCommand("CTRL_DOWN");
    ui.clear();

    QCOMPARE(QString::fromStdString(session.handleCommand("CANCEL")), QStringLiteral("CANCELLED"));
    QCOMPARE(recorder.stops(), 1);
    QVERIFY(asr.audioPaths().empty());
    QVERIFY(committer.texts().empty());
    QCOMPARE(session.state(), SessionState::Idle);
    QCOMPARE(QString::fromStdString(ui.messages().back()), QStringLiteral("IDLE"));
}

void TestVoiceSession::blurWhileRecordingCancelsAndDiscards()
{
    FakeRecorder recorder;
    FakeAsr asr;
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeSession(recorder, asr, committer, ui);

    session.handleCommand("CTRL_DOWN");
    QCOMPARE(QString::fromStdString(session.handleCommand("BLUR")), QStringLiteral("TOOLTIP hide"));
    QCOMPARE(recorder.stops(), 1);
    QVERIFY(committer.texts().empty());
    QCOMPARE(session.state(), SessionState::Idle);
    QCOMPARE(QString::fromStdString(ui.messages().back()), QStringLiteral("IDLE"));
}

void TestVoiceSession::liveCtrlStartsPipeline()
{
    FakeLivePipeline pipeline;
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeLiveSession(pipeline, committer, ui);

    QCOMPARE(
        QString::fromStdString(session.handleCommand("CTRL_DOWN")), QStringLiteral("RECORDING"));
    QCOMPARE(pipeline.starts(), 1);
    QCOMPARE(session.state(), SessionState::Recording);
    QCOMPARE(QString::fromStdString(ui.messages().back()), QStringLiteral("RECORDING"));
}

void TestVoiceSession::liveSecondCtrlFinishesAndCommits()
{
    FakeLivePipeline pipeline;
    pipeline.result = "实时语音输入";
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeLiveSession(pipeline, committer, ui);

    session.handleCommand("CTRL_DOWN");

    QCOMPARE(
        QString::fromStdString(session.handleCommand("CTRL_DOWN")), QStringLiteral("TRANSCRIBING"));
    QTRY_COMPARE(session.state(), SessionState::Idle);
    QVERIFY(ui.waitForMessage("IDLE"));
    QCOMPARE(pipeline.finishes(), 1);
    const auto texts = committer.texts();
    QCOMPARE(texts.size(), size_t(1));
    QCOMPARE(QString::fromStdString(texts.front()), QStringLiteral("实时语音输入"));
    const auto messages = ui.messages();
    QCOMPARE(
        QString::fromStdString(messages.at(messages.size() - 2)), QStringLiteral("TRANSCRIBING"));
    QCOMPARE(QString::fromStdString(messages.back()), QStringLiteral("IDLE"));
}

void TestVoiceSession::liveEmptyResultDoesNotCommit()
{
    FakeLivePipeline pipeline;
    pipeline.result.clear();
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeLiveSession(pipeline, committer, ui);

    session.handleCommand("CTRL_DOWN");

    QCOMPARE(
        QString::fromStdString(session.handleCommand("CTRL_DOWN")), QStringLiteral("TRANSCRIBING"));
    QTRY_COMPARE(session.state(), SessionState::Idle);
    QVERIFY(ui.waitForMessage("IDLE"));
    QCOMPARE(pipeline.finishes(), 1);
    QVERIFY(committer.texts().empty());
    const auto messages = ui.messages();
    QCOMPARE(
        QString::fromStdString(messages.at(messages.size() - 2)), QStringLiteral("TRANSCRIBING"));
    QCOMPARE(QString::fromStdString(messages.back()), QStringLiteral("IDLE"));
}

void TestVoiceSession::liveFinishExceptionReturnsToIdle()
{
    FakeLivePipeline pipeline;
    pipeline.throwOnFinish = true;
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeLiveSession(pipeline, committer, ui);

    session.handleCommand("CTRL_DOWN");

    QCOMPARE(
        QString::fromStdString(session.handleCommand("CTRL_DOWN")), QStringLiteral("TRANSCRIBING"));
    QTRY_COMPARE(session.state(), SessionState::Idle);
    QVERIFY(ui.waitForMessage("IDLE"));
    QCOMPARE(pipeline.finishes(), 1);
    QVERIFY(committer.texts().empty());
    const auto messages = ui.messages();
    QCOMPARE(
        QString::fromStdString(messages.at(messages.size() - 2)), QStringLiteral("TRANSCRIBING"));
    QCOMPARE(QString::fromStdString(messages.back()), QStringLiteral("IDLE"));
}

void TestVoiceSession::liveCancelDiscardsRecording()
{
    // The capsule's X button sends CANCEL: cancel the live pipeline, discard,
    // never finish/commit, and emit IDLE so the UI hides.
    FakeLivePipeline pipeline;
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeLiveSession(pipeline, committer, ui);

    session.handleCommand("CTRL_DOWN");
    ui.clear();

    QCOMPARE(QString::fromStdString(session.handleCommand("CANCEL")), QStringLiteral("CANCELLED"));
    QCOMPARE(pipeline.cancels(), 1);
    QCOMPARE(pipeline.finishes(), 0);
    QVERIFY(committer.texts().empty());
    QCOMPARE(session.state(), SessionState::Idle);
    QCOMPARE(QString::fromStdString(ui.messages().back()), QStringLiteral("IDLE"));
}

void TestVoiceSession::liveBlurCancelsAndDiscards()
{
    FakeLivePipeline pipeline;
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeLiveSession(pipeline, committer, ui);

    session.handleCommand("CTRL_DOWN");
    QCOMPARE(QString::fromStdString(session.handleCommand("BLUR")), QStringLiteral("TOOLTIP hide"));
    QCOMPARE(pipeline.cancels(), 1);
    QCOMPARE(pipeline.finishes(), 0);
    QVERIFY(committer.texts().empty());
    QCOMPARE(session.state(), SessionState::Idle);
    QCOMPARE(QString::fromStdString(ui.messages().back()), QStringLiteral("IDLE"));
}

void TestVoiceSession::liveCancelExceptionStillReturnsToIdleAndHidesTooltip()
{
    FakeLivePipeline pipeline;
    pipeline.throwOnCancel = true;
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeLiveSession(pipeline, committer, ui);

    session.handleCommand("CTRL_DOWN");
    QCOMPARE(QString::fromStdString(session.handleCommand("BLUR")), QStringLiteral("TOOLTIP hide"));
    QCOMPARE(pipeline.cancels(), 1);
    QCOMPARE(pipeline.finishes(), 0);
    QVERIFY(committer.texts().empty());
    QCOMPARE(session.state(), SessionState::Idle);
    QCOMPARE(QString::fromStdString(ui.messages().back()), QStringLiteral("IDLE"));
}

void TestVoiceSession::livePartialTextUpdatesTooltip()
{
    FakeLivePipeline pipeline;
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeLiveSession(pipeline, committer, ui);

    const auto callback = pipeline.callback();
    QVERIFY(callback);
    // Partials only flow while a recording is active.
    session.handleCommand("CTRL_DOWN");
    ui.clear();
    callback("实时文本");

    QCOMPARE(QString::fromStdString(ui.messages().back()), QStringLiteral("STREAM_TEXT 实时文本"));

    // After CANCEL, late partials are dropped (the session the user closed).
    session.handleCommand("CANCEL");
    ui.clear();
    callback("迟到文本");
    QVERIFY(ui.messages().empty());
}

void TestVoiceSession::livePreviewDoesNotAffectCommittedText()
{
    FakeLivePipeline pipeline;
    pipeline.result = "稳定最终文本";
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeLiveSession(pipeline, committer, ui);

    session.handleCommand("CTRL_DOWN");
    const auto callback = pipeline.callback();
    QVERIFY(callback);
    callback("错误预览文本");

    QCOMPARE(
        QString::fromStdString(session.handleCommand("CTRL_DOWN")), QStringLiteral("TRANSCRIBING"));
    QTRY_COMPARE(session.state(), SessionState::Idle);
    QVERIFY(ui.waitForMessage("IDLE"));
    const auto texts = committer.texts();
    QCOMPARE(texts.size(), size_t(1));
    QCOMPARE(QString::fromStdString(texts.front()), QStringLiteral("稳定最终文本"));
}

void TestVoiceSession::liveStartExceptionReturnsToIdle()
{
    FakeLivePipeline pipeline;
    pipeline.throwOnStart = true;
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeLiveSession(pipeline, committer, ui);

    QCOMPARE(QString::fromStdString(session.handleCommand("CTRL_DOWN")),
        QStringLiteral("ERR live start unavailable"));
    QCOMPARE(pipeline.starts(), 1);
    QCOMPARE(session.state(), SessionState::Idle);
    QCOMPARE(QString::fromStdString(ui.messages().back()), QStringLiteral("IDLE"));
}

void TestVoiceSession::stopReturnsWhileLiveFinishIsBlocked()
{
    BlockingLivePipeline pipeline;
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeLiveSession(pipeline, committer, ui);

    session.handleCommand("CTRL_DOWN");
    auto stopResult
        = std::async(std::launch::async, [&session] { return session.handleCommand("CTRL_DOWN"); });
    QVERIFY(pipeline.waitForFinishEntered());

    const bool returnedPromptly = stopResult.wait_for(200ms) == std::future_status::ready;
    pipeline.releaseFinish();
    QCOMPARE(QString::fromStdString(stopResult.get()), QStringLiteral("TRANSCRIBING"));
    QVERIFY2(returnedPromptly, "CTRL_DOWN waited for livePipeline.finish()");
    QTRY_COMPARE(session.state(), SessionState::Idle);
    QVERIFY(ui.waitForMessage("IDLE"));
}

void TestVoiceSession::cancelDuringTranscribingDiscardsFinishedText()
{
    BlockingLivePipeline pipeline;
    pipeline.result = "must be discarded";
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeLiveSession(pipeline, committer, ui);

    session.handleCommand("CTRL_DOWN");
    QCOMPARE(
        QString::fromStdString(session.handleCommand("CTRL_DOWN")), QStringLiteral("TRANSCRIBING"));
    QVERIFY(pipeline.waitForFinishEntered());

    QCOMPARE(QString::fromStdString(session.handleCommand("CANCEL")), QStringLiteral("CANCELLED"));
    QCOMPARE(session.state(), SessionState::Cancelling);
    QCOMPARE(QString::fromStdString(ui.messages().back()), QStringLiteral("CANCELLING"));
    QCOMPARE(pipeline.cancels(), 0);

    QCOMPARE(QString::fromStdString(session.handleCommand("CANCEL")), QStringLiteral("CANCELLED"));
    QCOMPARE(session.state(), SessionState::Cancelling);
    QCOMPARE(QString::fromStdString(ui.messages().back()), QStringLiteral("CANCELLING"));

    pipeline.releaseFinish();
    QTRY_COMPARE(session.state(), SessionState::Idle);
    QVERIFY(ui.waitForMessage("IDLE"));
    QVERIFY(committer.texts().empty());
    QCOMPARE(QString::fromStdString(ui.messages().back()), QStringLiteral("IDLE"));
}

void TestVoiceSession::ctrlDuringCancellingDoesNotRestartPipeline()
{
    BlockingLivePipeline pipeline;
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeLiveSession(pipeline, committer, ui);

    session.handleCommand("CTRL_DOWN");
    session.handleCommand("CTRL_DOWN");
    QVERIFY(pipeline.waitForFinishEntered());
    session.handleCommand("CANCEL");

    QCOMPARE(
        QString::fromStdString(session.handleCommand("CTRL_DOWN")), QStringLiteral("CANCELLING"));
    QCOMPARE(pipeline.starts(), 1);
    QCOMPARE(pipeline.finishes(), 1);

    pipeline.releaseFinish();
    QTRY_COMPARE(session.state(), SessionState::Idle);
    QVERIFY(ui.waitForMessage("IDLE"));
}

void TestVoiceSession::cancellingNotificationPrecedesFinalIdle()
{
    BlockingLivePipeline pipeline;
    pipeline.result = "must be discarded";
    FakeCommitter committer;
    BlockingCancellationUi ui;
    VoiceSession session(Config::defaultConfig(), pipeline, committer, ui);

    session.handleCommand("CTRL_DOWN");
    QCOMPARE(
        QString::fromStdString(session.handleCommand("CTRL_DOWN")), QStringLiteral("TRANSCRIBING"));
    const bool finishEntered = pipeline.waitForFinishEntered();
    auto cancelResult
        = std::async(std::launch::async, [&session] { return session.handleCommand("CANCEL"); });
    const bool cancellationEntered = ui.waitForCancellationEntered();

    pipeline.releaseFinish();
    const bool finishExited = pipeline.waitForFinishExited();
    const bool idlePublishedWhileCancellationBlocked = ui.waitForMessage("IDLE", 250ms);
    ui.releaseCancellation();
    const auto result = cancelResult.get();
    const bool idlePublished = ui.waitForMessage("IDLE");
    const auto messages = ui.messages();

    QVERIFY(finishEntered);
    QVERIFY(cancellationEntered);
    QVERIFY(finishExited);
    QVERIFY2(!idlePublishedWhileCancellationBlocked,
        "IDLE overtook the accepted CANCELLING notification");
    QCOMPARE(QString::fromStdString(result), QStringLiteral("CANCELLED"));
    QVERIFY(idlePublished);
    QVERIFY(messages.size() >= size_t(2));
    QCOMPARE(QString::fromStdString(messages.at(messages.size() - 2)), QStringLiteral("CANCELLING"));
    QCOMPARE(QString::fromStdString(messages.back()), QStringLiteral("IDLE"));
    QVERIFY(committer.texts().empty());
}

void TestVoiceSession::statusReportsEveryPhaseWithoutMutation()
{
    BlockingLivePipeline pipeline;
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeLiveSession(pipeline, committer, ui);

    QCOMPARE(QString::fromStdString(session.handleCommand("STATUS")), QStringLiteral("IDLE"));
    QCOMPARE(session.state(), SessionState::Idle);
    QCOMPARE(QString::fromStdString(ui.messages().back()), QStringLiteral("IDLE"));

    session.handleCommand("CTRL_DOWN");
    QCOMPARE(QString::fromStdString(session.handleCommand("STATUS")), QStringLiteral("RECORDING"));
    QCOMPARE(session.state(), SessionState::Recording);
    QCOMPARE(QString::fromStdString(ui.messages().back()), QStringLiteral("RECORDING"));

    session.handleCommand("CTRL_DOWN");
    QVERIFY(pipeline.waitForFinishEntered());
    QCOMPARE(
        QString::fromStdString(session.handleCommand("STATUS")), QStringLiteral("TRANSCRIBING"));
    QCOMPARE(session.state(), SessionState::Transcribing);
    QCOMPARE(QString::fromStdString(ui.messages().back()), QStringLiteral("TRANSCRIBING"));

    session.handleCommand("CANCEL");
    QCOMPARE(QString::fromStdString(session.handleCommand("STATUS")), QStringLiteral("CANCELLING"));
    QCOMPARE(session.state(), SessionState::Cancelling);
    QCOMPARE(QString::fromStdString(ui.messages().back()), QStringLiteral("CANCELLING"));

    pipeline.releaseFinish();
    QTRY_COMPARE(session.state(), SessionState::Idle);
    QVERIFY(ui.waitForMessage("IDLE"));
}

void TestVoiceSession::blurDuringTranscribingDiscardsFinishedText()
{
    BlockingLivePipeline pipeline;
    pipeline.result = "must be discarded";
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeLiveSession(pipeline, committer, ui);

    session.handleCommand("CTRL_DOWN");
    session.handleCommand("CTRL_DOWN");
    QVERIFY(pipeline.waitForFinishEntered());

    QCOMPARE(QString::fromStdString(session.handleCommand("BLUR")), QStringLiteral("TOOLTIP hide"));
    QCOMPARE(session.state(), SessionState::Cancelling);
    QCOMPARE(QString::fromStdString(ui.messages().back()), QStringLiteral("CANCELLING"));
    QCOMPARE(pipeline.cancels(), 0);

    pipeline.releaseFinish();
    QTRY_COMPARE(session.state(), SessionState::Idle);
    QVERIFY(ui.waitForMessage("IDLE"));
    QVERIFY(committer.texts().empty());
}

void TestVoiceSession::destructorWaitsForFinalization()
{
    BlockingLivePipeline pipeline;
    FakeCommitter committer;
    FakeUi ui;
    auto lifetime = std::async(std::launch::async, [&] {
        auto session = makeLiveSession(pipeline, committer, ui);
        session.handleCommand("CTRL_DOWN");
        return session.handleCommand("CTRL_DOWN");
    });

    QVERIFY(pipeline.waitForFinishEntered());
    QCOMPARE(lifetime.wait_for(200ms), std::future_status::timeout);
    pipeline.releaseFinish();
    QCOMPARE(lifetime.wait_for(1000ms), std::future_status::ready);
    QCOMPARE(QString::fromStdString(lifetime.get()), QStringLiteral("TRANSCRIBING"));
    QVERIFY(!pipeline.callback());
}

void TestVoiceSession::latePartialAfterStopIsIgnored()
{
    BlockingLivePipeline pipeline;
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeLiveSession(pipeline, committer, ui);

    session.handleCommand("CTRL_DOWN");
    const auto callback = pipeline.callback();
    QVERIFY(callback);
    session.handleCommand("CTRL_DOWN");
    QVERIFY(pipeline.waitForFinishEntered());
    ui.clear();

    callback("late text");
    QVERIFY(ui.messages().empty());

    pipeline.releaseFinish();
    QTRY_COMPARE(session.state(), SessionState::Idle);
    QVERIFY(ui.waitForMessage("IDLE"));
}

void TestVoiceSession::unknownCommandReturnsError()
{
    FakeRecorder recorder;
    FakeAsr asr;
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeSession(recorder, asr, committer, ui);

    QCOMPARE(QString::fromStdString(session.handleCommand("FROBNICATE")),
        QStringLiteral("ERR unknown-command"));
}

QTEST_GUILESS_MAIN(TestVoiceSession)
#include "test_voice_session.moc"
