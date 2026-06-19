// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "VoiceSession.h"

#include <stdexcept>
#include <vector>

using namespace echoflow;

struct FakeRecorder : IRecorder {
    int starts = 0;
    int stops = 0;
    std::filesystem::path path = "/tmp/echoflow-test.wav";

    void start() override { ++starts; }
    std::filesystem::path stop() override
    {
        ++stops;
        return path;
    }
};

struct FakeAsr : IAsrEngine {
    explicit FakeAsr(std::string r = "hello") : result(std::move(r)) {}

    std::string result;
    bool throwOnTranscribe = false;
    std::vector<std::filesystem::path> audioPaths;

    std::string transcribe(const std::filesystem::path& audio) override
    {
        audioPaths.push_back(audio);
        if (throwOnTranscribe) {
            throw std::runtime_error("asr unavailable");
        }
        return result;
    }
};

struct FakeLivePipeline : ILiveVoicePipeline {
    int starts = 0;
    int finishes = 0;
    int cancels = 0;
    std::string result = "hello";
    bool throwOnStart = false;
    bool throwOnFinish = false;

    void start() override
    {
        ++starts;
        if (throwOnStart) {
            throw std::runtime_error("live start unavailable");
        }
    }

    std::string finish() override
    {
        ++finishes;
        if (throwOnFinish) {
            throw std::runtime_error("live finish unavailable");
        }
        return result;
    }

    void cancel() override { ++cancels; }
};

struct FakeCommitter : ICommitter {
    std::vector<std::string> texts;
    std::pair<bool, std::string> ret = {true, "OK"};

    std::pair<bool, std::string> commitText(const std::string& text) override
    {
        texts.push_back(text);
        return ret;
    }
};

struct FakeUi : IUiNotifier {
    std::vector<std::string> messages;
    void send(const std::string& message) override { messages.push_back(message); }
};

class TestVoiceSession : public QObject {
    Q_OBJECT

private slots:
    void focusThenCtrlStartsRecording();
    void focusWithCursorRectForwardsTooltipPosition();
    void typedHidesTooltipWhenIdle();
    void focusAfterTypedStaysHiddenUntilBlur();
    void typedWhileRecordingIsIgnored();
    void secondCtrlStopsTranscribesAndCommits();
    void transcribeExceptionReturnsToIdle();
    void tooShortAudioIsCancelled();
    void blurWhileRecordingCancelsAndDiscards();
    void liveCtrlStartsPipeline();
    void liveSecondCtrlFinishesAndCommits();
    void liveEmptyResultDoesNotCommit();
    void liveFinishExceptionReturnsToIdle();
    void liveBlurCancelsAndDiscards();
    void liveStartExceptionReturnsToIdle();
    void unknownCommandReturnsError();
};

static VoiceSession makeSession(FakeRecorder& recorder, FakeAsr& asr,
                                FakeCommitter& committer, FakeUi& ui)
{
    return VoiceSession(Config::defaultConfig(), recorder, asr, committer, ui);
}

static VoiceSession makeLiveSession(FakeLivePipeline& pipeline,
                                    FakeCommitter& committer, FakeUi& ui)
{
    return VoiceSession(Config::defaultConfig(), pipeline, committer, ui);
}

void TestVoiceSession::focusThenCtrlStartsRecording()
{
    FakeRecorder recorder;
    FakeAsr asr;
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeSession(recorder, asr, committer, ui);

    QCOMPARE(QString::fromStdString(session.handleCommand("FOCUS")), QStringLiteral("TOOLTIP show"));
    QCOMPARE(QString::fromStdString(ui.messages.back()), QStringLiteral("SHOW_TOOLTIP 按右 Ctrl 语音输入"));

    QCOMPARE(QString::fromStdString(session.handleCommand("CTRL_DOWN")), QStringLiteral("RECORDING"));
    QCOMPARE(recorder.starts, 1);
    QCOMPARE(session.state(), SessionState::Recording);
    QVERIFY(session.tooltipVisible());
    QCOMPARE(QString::fromStdString(ui.messages.back()), QStringLiteral("RECORDING"));
}

void TestVoiceSession::focusWithCursorRectForwardsTooltipPosition()
{
    FakeRecorder recorder;
    FakeAsr asr;
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeSession(recorder, asr, committer, ui);

    QCOMPARE(QString::fromStdString(session.handleCommand("FOCUS 120 240 2 18")), QStringLiteral("TOOLTIP show"));
    QCOMPARE(QString::fromStdString(ui.messages.back()),
             QStringLiteral("SHOW_TOOLTIP 120 240 2 18 按右 Ctrl 语音输入"));
}

void TestVoiceSession::typedHidesTooltipWhenIdle()
{
    FakeRecorder recorder;
    FakeAsr asr;
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeSession(recorder, asr, committer, ui);
    session.handleCommand("FOCUS");
    ui.messages.clear();

    QCOMPARE(QString::fromStdString(session.handleCommand("TYPED")), QStringLiteral("TYPING hide"));
    QCOMPARE(QString::fromStdString(ui.messages.back()), QStringLiteral("HIDE_TOOLTIP"));
    QVERIFY(session.typedHidden());
}

void TestVoiceSession::focusAfterTypedStaysHiddenUntilBlur()
{
    FakeRecorder recorder;
    FakeAsr asr;
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeSession(recorder, asr, committer, ui);
    session.handleCommand("FOCUS");
    session.handleCommand("TYPED");
    ui.messages.clear();

    QCOMPARE(QString::fromStdString(session.handleCommand("FOCUS 120 240 2 18")),
             QStringLiteral("TOOLTIP suppressed"));
    QVERIFY(ui.messages.empty());

    QCOMPARE(QString::fromStdString(session.handleCommand("BLUR")), QStringLiteral("TOOLTIP hide"));
    ui.messages.clear();
    QCOMPARE(QString::fromStdString(session.handleCommand("FOCUS")), QStringLiteral("TOOLTIP show"));
    QCOMPARE(QString::fromStdString(ui.messages.back()), QStringLiteral("SHOW_TOOLTIP 按右 Ctrl 语音输入"));
}

void TestVoiceSession::typedWhileRecordingIsIgnored()
{
    FakeRecorder recorder;
    FakeAsr asr;
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeSession(recorder, asr, committer, ui);
    session.handleCommand("FOCUS");
    session.handleCommand("CTRL_DOWN");
    ui.messages.clear();

    QCOMPARE(QString::fromStdString(session.handleCommand("TYPED")), QStringLiteral("IGNORED"));
    QVERIFY(ui.messages.empty());
    QCOMPARE(session.state(), SessionState::Recording);
}

void TestVoiceSession::secondCtrlStopsTranscribesAndCommits()
{
    FakeRecorder recorder;
    FakeAsr asr("离线语音输入");
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeSession(recorder, asr, committer, ui);

    session.handleCommand("FOCUS");
    session.handleCommand("CTRL_DOWN");

    QCOMPARE(QString::fromStdString(session.handleCommand("CTRL_DOWN")), QStringLiteral("COMMITTED"));
    QCOMPARE(recorder.stops, 1);
    QCOMPARE(asr.audioPaths.size(), size_t(1));
    QCOMPARE(asr.audioPaths.front(), recorder.path);
    QCOMPARE(committer.texts.size(), size_t(1));
    QCOMPARE(QString::fromStdString(committer.texts.front()), QStringLiteral("离线语音输入"));
    QCOMPARE(session.state(), SessionState::Idle);
    QCOMPARE(QString::fromStdString(ui.messages.at(ui.messages.size() - 2)), QStringLiteral("TRANSCRIBING"));
    QCOMPARE(QString::fromStdString(ui.messages.back()), QStringLiteral("IDLE"));
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

    QCOMPARE(QString::fromStdString(session.handleCommand("CTRL_DOWN")), QStringLiteral("EMPTY"));
    QCOMPARE(session.state(), SessionState::Idle);
    QVERIFY(committer.texts.empty());
    QCOMPARE(QString::fromStdString(ui.messages.at(ui.messages.size() - 2)), QStringLiteral("TRANSCRIBING"));
    QCOMPARE(QString::fromStdString(ui.messages.back()), QStringLiteral("IDLE"));
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
    QCOMPARE(QString::fromStdString(session.handleCommand("CTRL_DOWN")), QStringLiteral("CANCELLED"));
    QCOMPARE(recorder.stops, 1);
    QVERIFY(committer.texts.empty());
    QCOMPARE(session.state(), SessionState::Idle);
}

void TestVoiceSession::blurWhileRecordingCancelsAndDiscards()
{
    FakeRecorder recorder;
    FakeAsr asr;
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeSession(recorder, asr, committer, ui);

    session.handleCommand("FOCUS");
    session.handleCommand("CTRL_DOWN");
    QCOMPARE(QString::fromStdString(session.handleCommand("BLUR")), QStringLiteral("TOOLTIP hide"));
    QCOMPARE(recorder.stops, 1);
    QVERIFY(committer.texts.empty());
    QCOMPARE(session.state(), SessionState::Idle);
    QCOMPARE(QString::fromStdString(ui.messages.back()), QStringLiteral("HIDE_TOOLTIP"));
}

void TestVoiceSession::liveCtrlStartsPipeline()
{
    FakeLivePipeline pipeline;
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeLiveSession(pipeline, committer, ui);

    QCOMPARE(QString::fromStdString(session.handleCommand("CTRL_DOWN")), QStringLiteral("RECORDING"));
    QCOMPARE(pipeline.starts, 1);
    QCOMPARE(session.state(), SessionState::Recording);
    QCOMPARE(QString::fromStdString(ui.messages.back()), QStringLiteral("RECORDING"));
}

void TestVoiceSession::liveSecondCtrlFinishesAndCommits()
{
    FakeLivePipeline pipeline;
    pipeline.result = "实时语音输入";
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeLiveSession(pipeline, committer, ui);

    session.handleCommand("CTRL_DOWN");

    QCOMPARE(QString::fromStdString(session.handleCommand("CTRL_DOWN")), QStringLiteral("COMMITTED"));
    QCOMPARE(pipeline.finishes, 1);
    QCOMPARE(committer.texts.size(), size_t(1));
    QCOMPARE(QString::fromStdString(committer.texts.front()), QStringLiteral("实时语音输入"));
    QCOMPARE(session.state(), SessionState::Idle);
    QCOMPARE(QString::fromStdString(ui.messages.at(ui.messages.size() - 2)), QStringLiteral("TRANSCRIBING"));
    QCOMPARE(QString::fromStdString(ui.messages.back()), QStringLiteral("IDLE"));
}

void TestVoiceSession::liveEmptyResultDoesNotCommit()
{
    FakeLivePipeline pipeline;
    pipeline.result.clear();
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeLiveSession(pipeline, committer, ui);

    session.handleCommand("CTRL_DOWN");

    QCOMPARE(QString::fromStdString(session.handleCommand("CTRL_DOWN")), QStringLiteral("EMPTY"));
    QCOMPARE(pipeline.finishes, 1);
    QVERIFY(committer.texts.empty());
    QCOMPARE(session.state(), SessionState::Idle);
    QCOMPARE(QString::fromStdString(ui.messages.at(ui.messages.size() - 2)), QStringLiteral("TRANSCRIBING"));
    QCOMPARE(QString::fromStdString(ui.messages.back()), QStringLiteral("IDLE"));
}

void TestVoiceSession::liveFinishExceptionReturnsToIdle()
{
    FakeLivePipeline pipeline;
    pipeline.throwOnFinish = true;
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeLiveSession(pipeline, committer, ui);

    session.handleCommand("CTRL_DOWN");

    QCOMPARE(QString::fromStdString(session.handleCommand("CTRL_DOWN")), QStringLiteral("EMPTY"));
    QCOMPARE(pipeline.finishes, 1);
    QVERIFY(committer.texts.empty());
    QCOMPARE(session.state(), SessionState::Idle);
    QCOMPARE(QString::fromStdString(ui.messages.at(ui.messages.size() - 2)), QStringLiteral("TRANSCRIBING"));
    QCOMPARE(QString::fromStdString(ui.messages.back()), QStringLiteral("IDLE"));
}

void TestVoiceSession::liveBlurCancelsAndDiscards()
{
    FakeLivePipeline pipeline;
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeLiveSession(pipeline, committer, ui);

    session.handleCommand("CTRL_DOWN");
    QCOMPARE(QString::fromStdString(session.handleCommand("BLUR")), QStringLiteral("TOOLTIP hide"));
    QCOMPARE(pipeline.cancels, 1);
    QCOMPARE(pipeline.finishes, 0);
    QVERIFY(committer.texts.empty());
    QCOMPARE(session.state(), SessionState::Idle);
    QCOMPARE(QString::fromStdString(ui.messages.back()), QStringLiteral("HIDE_TOOLTIP"));
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
    QCOMPARE(pipeline.starts, 1);
    QCOMPARE(session.state(), SessionState::Idle);
    QCOMPARE(QString::fromStdString(ui.messages.back()), QStringLiteral("IDLE"));
}

void TestVoiceSession::unknownCommandReturnsError()
{
    FakeRecorder recorder;
    FakeAsr asr;
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeSession(recorder, asr, committer, ui);

    QCOMPARE(QString::fromStdString(session.handleCommand("FROBNICATE")), QStringLiteral("ERR unknown-command"));
}

QTEST_GUILESS_MAIN(TestVoiceSession)
#include "test_voice_session.moc"
