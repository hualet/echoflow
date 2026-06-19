# Live Stream ASR Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Start ASR while the user is recording, then submit only the final text after the second right Ctrl.

**Architecture:** Add an `ILiveVoicePipeline` boundary that owns the live recorder and ASR session, while `VoiceSession` keeps the existing tap-to-talk state machine. Production uses a `pw-record` raw PCM stdout pipe, a qwen-compatible live audio buffer, and an ASR worker calling `qwen_transcribe_stream_live()`.

**Tech Stack:** C++17, Qt Test, POSIX pipes/spawn/wait, pthread-compatible `qwen_live_audio_t`, qwen-asr C API, CMake.

---

## File Structure

- Modify `service/Interfaces.h`: add `ILiveVoicePipeline`.
- Modify `service/VoiceSession.h/.cpp`: add live-pipeline constructor path and route recording commands through it when present.
- Modify `tests/test_voice_session.cpp`: add fake live pipeline tests before implementation.
- Create `service/LiveAudioBuffer.h/.cpp`: own `qwen_live_audio_t` allocation, EOF signaling, and `s16le` conversion/append helpers.
- Create `tests/test_live_audio_buffer.cpp`: unit tests for PCM conversion and idempotent EOF.
- Modify `service/AsrEngine.h/.cpp`: add `transcribeLive(qwen_live_audio_t*)` sharing existing model setup.
- Create `service/PipeWireLiveVoicePipeline.h/.cpp`: production raw PCM recorder and worker-thread implementation.
- Modify `service/main.cpp`: wire production `VoiceSession` to `PipeWireLiveVoicePipeline`.
- Modify `service/CMakeLists.txt` and `tests/CMakeLists.txt`: add new sources/tests.

---

### Task 1: Add the Live Pipeline Contract and VoiceSession Behavior

**Files:**
- Modify: `service/Interfaces.h`
- Modify: `service/VoiceSession.h`
- Modify: `service/VoiceSession.cpp`
- Test: `tests/test_voice_session.cpp`

- [ ] **Step 1: Write failing VoiceSession live-pipeline tests**

Add this fake to `tests/test_voice_session.cpp` after `FakeAsr`:

```cpp
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
            throw std::runtime_error("live start failed");
        }
    }

    std::string finish() override
    {
        ++finishes;
        if (throwOnFinish) {
            throw std::runtime_error("live finish failed");
        }
        return result;
    }

    void cancel() override { ++cancels; }
};
```

Add this helper after the existing `makeSession(...)`:

```cpp
static VoiceSession makeLiveSession(FakeLivePipeline& pipeline,
                                    FakeCommitter& committer, FakeUi& ui)
{
    return VoiceSession(Config::defaultConfig(), pipeline, committer, ui);
}
```

Add these test slots to `TestVoiceSession`:

```cpp
void liveCtrlStartsPipeline();
void liveSecondCtrlFinishesAndCommits();
void liveEmptyResultDoesNotCommit();
void liveFinishExceptionReturnsToIdle();
void liveBlurCancelsAndDiscards();
void liveStartExceptionReturnsToIdle();
```

Add these test bodies before `unknownCommandReturnsError()`:

```cpp
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
    pipeline.result = "流式语音输入";
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeLiveSession(pipeline, committer, ui);

    session.handleCommand("CTRL_DOWN");
    QCOMPARE(QString::fromStdString(session.handleCommand("CTRL_DOWN")), QStringLiteral("COMMITTED"));

    QCOMPARE(pipeline.finishes, 1);
    QCOMPARE(pipeline.cancels, 0);
    QCOMPARE(committer.texts.size(), size_t(1));
    QCOMPARE(QString::fromStdString(committer.texts.front()), QStringLiteral("流式语音输入"));
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

    QCOMPARE(QString::fromStdString(session.handleCommand("CTRL_DOWN")), QStringLiteral("ERR live start failed"));

    QCOMPARE(pipeline.starts, 1);
    QCOMPARE(session.state(), SessionState::Idle);
    QCOMPARE(QString::fromStdString(ui.messages.back()), QStringLiteral("IDLE"));
}
```

- [ ] **Step 2: Run the VoiceSession test and verify it fails**

Run:

```bash
cmake --build build --target test_voice_session
./build/tests/test_voice_session
```

Expected: compile fails because `ILiveVoicePipeline` and the new `VoiceSession` constructor do not exist.

- [ ] **Step 3: Add `ILiveVoicePipeline`**

Append this to `service/Interfaces.h` after `IAsrEngine`:

```cpp
class ILiveVoicePipeline {
public:
    virtual ~ILiveVoicePipeline() = default;
    virtual void start() = 0;
    virtual std::string finish() = 0;
    virtual void cancel() = 0;
};
```

- [ ] **Step 4: Add the live constructor and nullable members**

Change the private members in `service/VoiceSession.h` to pointers for the old path and a nullable live pipeline:

```cpp
class VoiceSession {
public:
    VoiceSession(Config cfg, IRecorder& recorder, IAsrEngine& asr,
                 ICommitter& committer, IUiNotifier& ui);
    VoiceSession(Config cfg, ILiveVoicePipeline& livePipeline,
                 ICommitter& committer, IUiNotifier& ui);

    std::string handleCommand(const std::string& command);
    SessionState state() const { return state_; }
    bool tooltipVisible() const { return tooltipVisible_; }
    bool typedHidden() const { return typedHidden_; }

private:
    std::string startRecording();
    std::string stopTranscribeCommit();

    Config cfg_;
    IRecorder* recorder_ = nullptr;
    IAsrEngine* asr_ = nullptr;
    ILiveVoicePipeline* livePipeline_ = nullptr;
    ICommitter& committer_;
    IUiNotifier& ui_;
    SessionState state_ = SessionState::Idle;
    bool tooltipVisible_ = false;
    bool typedHidden_ = false;
};
```

- [ ] **Step 5: Route VoiceSession through the live pipeline when present**

Update `service/VoiceSession.cpp` constructors:

```cpp
VoiceSession::VoiceSession(Config cfg, IRecorder& recorder, IAsrEngine& asr,
                           ICommitter& committer, IUiNotifier& ui)
    : cfg_(std::move(cfg))
    , recorder_(&recorder)
    , asr_(&asr)
    , committer_(committer)
    , ui_(ui)
{
}

VoiceSession::VoiceSession(Config cfg, ILiveVoicePipeline& livePipeline,
                           ICommitter& committer, IUiNotifier& ui)
    : cfg_(std::move(cfg))
    , livePipeline_(&livePipeline)
    , committer_(committer)
    , ui_(ui)
{
}
```

Update recording cancellation in `handleCommand()`:

```cpp
if (verb == "BLUR") {
    tooltipVisible_ = false;
    typedHidden_ = false;
    if (state_ == SessionState::Recording) {
        if (livePipeline_) {
            livePipeline_->cancel();
        } else if (recorder_) {
            recorder_->stop();
        }
    }
    state_ = SessionState::Idle;
    ui_.send("HIDE_TOOLTIP");
    return "TOOLTIP hide";
}
```

Update `startRecording()`:

```cpp
std::string VoiceSession::startRecording()
{
    try {
        if (livePipeline_) {
            livePipeline_->start();
        } else if (recorder_) {
            recorder_->start();
        }
    } catch (const std::exception& e) {
        log(std::string("voice start failed: ") + e.what());
        state_ = SessionState::Idle;
        ui_.send("IDLE");
        return std::string("ERR ") + e.what();
    }
    ui_.send("RECORDING");
    state_ = SessionState::Recording;
    return "RECORDING";
}
```

Update the start of `stopTranscribeCommit()`:

```cpp
std::filesystem::path audio;
std::string text;
try {
    if (livePipeline_) {
        text = livePipeline_->finish();
    } else {
        audio = recorder_->stop();
        if (audio.empty()) {
            state_ = SessionState::Idle;
            ui_.send("IDLE");
            return "CANCELLED";
        }
        text = asr_->transcribe(audio);
    }
} catch (const std::exception& e) {
    log(std::string("asr transcribe failed: ") + e.what());
}
```

Remove the old duplicated `recorder_.stop()` and `asr_.transcribe(audio)` block from the same function.

- [ ] **Step 6: Run the VoiceSession test and verify it passes**

Run:

```bash
cmake --build build --target test_voice_session
./build/tests/test_voice_session
```

Expected: all `test_voice_session` cases pass.

- [ ] **Step 7: Commit Task 1**

```bash
git add service/Interfaces.h service/VoiceSession.h service/VoiceSession.cpp tests/test_voice_session.cpp
git commit -m "feat(service): add live voice session path" \
  -m "Introduce a live pipeline contract for tap-to-talk recording so VoiceSession can start recognition before stop." \
  -m "Keep the existing file-based recorder path for tests and command-line transcription while adding focused live-session coverage."
```

---

### Task 2: Add Live Audio Buffer Helpers

**Files:**
- Create: `service/LiveAudioBuffer.h`
- Create: `service/LiveAudioBuffer.cpp`
- Modify: `service/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Test: `tests/test_live_audio_buffer.cpp`

- [ ] **Step 1: Write failing live audio buffer tests**

Create `tests/test_live_audio_buffer.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "LiveAudioBuffer.h"

using namespace echoflow;

class TestLiveAudioBuffer : public QObject {
    Q_OBJECT

private slots:
    void appendS16leConvertsSamples();
    void markEofIsIdempotent();
};

void TestLiveAudioBuffer::appendS16leConvertsSamples()
{
    LiveAudioBuffer buffer;
    const unsigned char pcm[] = {
        0x00, 0x00,  // 0
        0x00, 0x40,  // 16384
        0x00, 0x80,  // -32768
        0xff, 0x7f,  // 32767
    };

    buffer.appendS16le(pcm, sizeof(pcm));
    qwen_live_audio_t* live = buffer.get();

    pthread_mutex_lock(&live->mutex);
    QCOMPARE(live->n_samples, int64_t(4));
    QCOMPARE(live->eof, 0);
    QVERIFY(std::abs(live->samples[0] - 0.0f) < 0.0001f);
    QVERIFY(std::abs(live->samples[1] - 0.5f) < 0.0001f);
    QVERIFY(std::abs(live->samples[2] + 1.0f) < 0.0001f);
    QVERIFY(live->samples[3] > 0.9999f);
    pthread_mutex_unlock(&live->mutex);
}

void TestLiveAudioBuffer::markEofIsIdempotent()
{
    LiveAudioBuffer buffer;
    buffer.markEof();
    buffer.markEof();
    qwen_live_audio_t* live = buffer.get();

    pthread_mutex_lock(&live->mutex);
    QCOMPARE(live->eof, 1);
    pthread_mutex_unlock(&live->mutex);
}

QTEST_GUILESS_MAIN(TestLiveAudioBuffer)
#include "test_live_audio_buffer.moc"
```

Add `test_live_audio_buffer` to `ECHOFLOW_TESTS` in `tests/CMakeLists.txt`.

- [ ] **Step 2: Run the new test and verify it fails**

Run:

```bash
cmake --build build --target test_live_audio_buffer
```

Expected: compile fails because `LiveAudioBuffer.h` does not exist.

- [ ] **Step 3: Add `LiveAudioBuffer.h`**

Create `service/LiveAudioBuffer.h`:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_LIVE_AUDIO_BUFFER_H
#define ECHOFLOW_LIVE_AUDIO_BUFFER_H

extern "C" {
#include "qwen_asr.h"
}

#include <cstddef>

namespace echoflow {

class LiveAudioBuffer {
public:
    LiveAudioBuffer();
    ~LiveAudioBuffer();

    LiveAudioBuffer(const LiveAudioBuffer&) = delete;
    LiveAudioBuffer& operator=(const LiveAudioBuffer&) = delete;

    qwen_live_audio_t* get() { return live_; }
    const qwen_live_audio_t* get() const { return live_; }

    void appendS16le(const unsigned char* data, std::size_t size);
    void appendFloatSamples(const float* samples, int nSamples);
    void markEof();

private:
    qwen_live_audio_t* live_ = nullptr;
};

}  // namespace echoflow

#endif
```

- [ ] **Step 4: Add `LiveAudioBuffer.cpp`**

Create `service/LiveAudioBuffer.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "LiveAudioBuffer.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace echoflow {

LiveAudioBuffer::LiveAudioBuffer()
{
    live_ = static_cast<qwen_live_audio_t*>(std::calloc(1, sizeof(qwen_live_audio_t)));
    if (!live_) {
        throw std::bad_alloc();
    }
    pthread_mutex_init(&live_->mutex, nullptr);
    pthread_cond_init(&live_->cond, nullptr);
}

LiveAudioBuffer::~LiveAudioBuffer()
{
    if (!live_) {
        return;
    }
    markEof();
    pthread_mutex_destroy(&live_->mutex);
    pthread_cond_destroy(&live_->cond);
    std::free(live_->samples);
    std::free(live_);
}

void LiveAudioBuffer::appendFloatSamples(const float* samples, int nSamples)
{
    if (!live_ || !samples || nSamples <= 0) {
        return;
    }

    pthread_mutex_lock(&live_->mutex);
    const int64_t need = live_->n_samples + static_cast<int64_t>(nSamples);
    if (need > live_->capacity) {
        int64_t newCapacity = live_->capacity > 0 ? live_->capacity : 32000;
        while (newCapacity < need) {
            newCapacity *= 2;
        }
        float* next = static_cast<float*>(
            std::realloc(live_->samples, static_cast<std::size_t>(newCapacity) * sizeof(float)));
        if (!next) {
            pthread_mutex_unlock(&live_->mutex);
            throw std::bad_alloc();
        }
        live_->samples = next;
        live_->capacity = newCapacity;
    }
    std::memcpy(live_->samples + live_->n_samples, samples,
                static_cast<std::size_t>(nSamples) * sizeof(float));
    live_->n_samples += nSamples;
    pthread_cond_signal(&live_->cond);
    pthread_mutex_unlock(&live_->mutex);
}

void LiveAudioBuffer::appendS16le(const unsigned char* data, std::size_t size)
{
    if (!data || size < 2) {
        return;
    }

    const std::size_t frames = size / 2;
    std::vector<float> samples(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        const auto lo = static_cast<unsigned int>(data[i * 2]);
        const auto hi = static_cast<unsigned int>(data[i * 2 + 1]);
        const int16_t value = static_cast<int16_t>(lo | (hi << 8));
        samples[i] = static_cast<float>(value) / 32768.0f;
    }
    appendFloatSamples(samples.data(), static_cast<int>(samples.size()));
}

void LiveAudioBuffer::markEof()
{
    if (!live_) {
        return;
    }
    pthread_mutex_lock(&live_->mutex);
    live_->eof = 1;
    pthread_cond_signal(&live_->cond);
    pthread_mutex_unlock(&live_->mutex);
}

}  // namespace echoflow
```

- [ ] **Step 5: Wire the new source into CMake**

Add `LiveAudioBuffer.cpp` to `add_library(echoflow_service STATIC ...)` in `service/CMakeLists.txt`.

- [ ] **Step 6: Run the new test and verify it passes**

Run:

```bash
cmake --build build --target test_live_audio_buffer
./build/tests/test_live_audio_buffer
```

Expected: all `test_live_audio_buffer` cases pass.

- [ ] **Step 7: Commit Task 2**

```bash
git add service/LiveAudioBuffer.h service/LiveAudioBuffer.cpp service/CMakeLists.txt tests/CMakeLists.txt tests/test_live_audio_buffer.cpp
git commit -m "feat(service): add qwen live audio buffer helper" \
  -m "Own qwen_live_audio_t allocation and EOF signaling inside the service instead of using qwen's stdin reader." \
  -m "Add unit coverage for raw s16le conversion because the live recorder will stream PCM bytes directly."
```

---

### Task 3: Add the qwen Live ASR Entry Point

**Files:**
- Modify: `service/AsrEngine.h`
- Modify: `service/AsrEngine.cpp`
- Test: existing `tests/test_asr_engine.cpp`

- [ ] **Step 1: Write the compile-facing API test**

Add this test slot to `tests/test_asr_engine.cpp`:

```cpp
void transcribeLiveReturnsEmptyWhenModelCannotLoad();
```

Add this test body before `QTEST_GUILESS_MAIN`:

```cpp
void TestAsrEngine::transcribeLiveReturnsEmptyWhenModelCannotLoad()
{
    Config cfg = Config::defaultConfig();
    cfg.modelDir = "/tmp/echoflow-model-that-does-not-exist";
    AsrEngine engine(cfg);
    LiveAudioBuffer buffer;
    buffer.markEof();

    try {
        QCOMPARE(QString::fromStdString(engine.transcribeLive(buffer.get())), QString());
    } catch (const std::exception& e) {
        QFAIL(qPrintable(QStringLiteral("transcribeLive threw: %1").arg(e.what())));
    }
}
```

Add `#include "LiveAudioBuffer.h"` near the existing `#include "AsrEngine.h"`.

- [ ] **Step 2: Run the ASR test and verify it fails**

Run:

```bash
cmake --build build --target test_asr_engine
```

Expected: compile fails because `AsrEngine::transcribeLive` does not exist.

- [ ] **Step 3: Add `transcribeLive` to `AsrEngine.h`**

Add this public method:

```cpp
std::string transcribeLive(qwen_live_audio_t* live);
```

- [ ] **Step 4: Implement `transcribeLive`**

Add this method to `service/AsrEngine.cpp` after `transcribe(...)`:

```cpp
std::string AsrEngine::transcribeLive(qwen_live_audio_t* live)
{
    if (!live || !ensureLoaded()) {
        return {};
    }

    auto started = std::chrono::steady_clock::now();
    log("transcribing live audio stream");
    char* raw = qwen_transcribe_stream_live(ctx_, live);
    if (!raw) {
        log("qwen_transcribe_stream_live returned empty result");
        return {};
    }

    std::string text(raw);
    std::free(raw);
    auto elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    log("live transcription finished in " + std::to_string(elapsed) +
        "s, chars=" + std::to_string(text.size()));
    return text;
}
```

- [ ] **Step 5: Run the ASR test and verify it passes**

Run:

```bash
cmake --build build --target test_asr_engine
./build/tests/test_asr_engine
```

Expected: all `test_asr_engine` cases pass.

- [ ] **Step 6: Commit Task 3**

```bash
git add service/AsrEngine.h service/AsrEngine.cpp tests/test_asr_engine.cpp
git commit -m "feat(service): expose live qwen transcription" \
  -m "Add an AsrEngine entry point for qwen_transcribe_stream_live so the production pipeline can run recognition while audio is still arriving." \
  -m "Keep the existing file transcription API for command-line transcription and fallback tooling."
```

---

### Task 4: Implement the PipeWire Live Voice Pipeline

**Files:**
- Create: `service/PipeWireLiveVoicePipeline.h`
- Create: `service/PipeWireLiveVoicePipeline.cpp`
- Modify: `service/CMakeLists.txt`
- Test: build-level coverage plus manual runtime verification

- [ ] **Step 1: Add the production class header**

Create `service/PipeWireLiveVoicePipeline.h`:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_PIPEWIRE_LIVE_VOICE_PIPELINE_H
#define ECHOFLOW_PIPEWIRE_LIVE_VOICE_PIPELINE_H

#include "AsrEngine.h"
#include "Config.h"
#include "Interfaces.h"
#include "LiveAudioBuffer.h"

#include <atomic>
#include <memory>
#include <string>
#include <sys/types.h>
#include <thread>

namespace echoflow {

class PipeWireLiveVoicePipeline : public ILiveVoicePipeline {
public:
    PipeWireLiveVoicePipeline(Config cfg, AsrEngine& asr);
    ~PipeWireLiveVoicePipeline() override;

    PipeWireLiveVoicePipeline(const PipeWireLiveVoicePipeline&) = delete;
    PipeWireLiveVoicePipeline& operator=(const PipeWireLiveVoicePipeline&) = delete;

    void start() override;
    std::string finish() override;
    void cancel() override;

private:
    void readerLoop();
    void asrLoop();
    void stopRecorder();
    void cleanupProcess();
    void closeReadFd();
    void joinThreads();

    Config cfg_;
    AsrEngine& asr_;
    std::unique_ptr<LiveAudioBuffer> live_;
    pid_t child_ = -1;
    int readFd_ = -1;
    std::thread readerThread_;
    std::thread asrThread_;
    std::string result_;
    std::atomic<bool> active_{false};
    std::atomic<bool> cancelled_{false};
};

}  // namespace echoflow

#endif
```

- [ ] **Step 2: Add the implementation**

Create `service/PipeWireLiveVoicePipeline.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PipeWireLiveVoicePipeline.h"

#include "log.h"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace echoflow {

PipeWireLiveVoicePipeline::PipeWireLiveVoicePipeline(Config cfg, AsrEngine& asr)
    : cfg_(std::move(cfg))
    , asr_(asr)
{
}

PipeWireLiveVoicePipeline::~PipeWireLiveVoicePipeline()
{
    cancel();
}

void PipeWireLiveVoicePipeline::start()
{
    if (active_) {
        return;
    }

    int pipeFds[2] = {-1, -1};
    if (pipe(pipeFds) != 0) {
        throw std::runtime_error(std::string("pipe failed: ") + std::strerror(errno));
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipeFds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipeFds[0]);
    posix_spawn_file_actions_addclose(&actions, pipeFds[1]);

    std::vector<std::string> args = {
        "pw-record",
        "--rate", "16000",
        "--channels", "1",
        "--format", "s16",
        "--raw",
        "-",
    };
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (auto& arg : args) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    pid_t pid = -1;
    const int spawnResult =
        posix_spawnp(&pid, "pw-record", &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pipeFds[1]);

    if (spawnResult != 0) {
        close(pipeFds[0]);
        throw std::runtime_error(std::string("posix_spawnp pw-record failed: ") +
                                 std::strerror(spawnResult));
    }

    child_ = pid;
    readFd_ = pipeFds[0];
    live_ = std::make_unique<LiveAudioBuffer>();
    cancelled_ = false;
    result_.clear();
    active_ = true;
    readerThread_ = std::thread(&PipeWireLiveVoicePipeline::readerLoop, this);
    asrThread_ = std::thread(&PipeWireLiveVoicePipeline::asrLoop, this);
    log("live recording started");
}

std::string PipeWireLiveVoicePipeline::finish()
{
    if (!active_) {
        return {};
    }
    stopRecorder();
    cleanupProcess();
    if (live_) {
        live_->markEof();
    }
    joinThreads();
    closeReadFd();
    active_ = false;
    live_.reset();
    return cancelled_ ? std::string() : result_;
}

void PipeWireLiveVoicePipeline::cancel()
{
    if (!active_ && child_ == -1 && readFd_ == -1) {
        return;
    }
    cancelled_ = true;
    stopRecorder();
    cleanupProcess();
    if (live_) {
        live_->markEof();
    }
    joinThreads();
    closeReadFd();
    active_ = false;
    live_.reset();
    result_.clear();
}

void PipeWireLiveVoicePipeline::readerLoop()
{
    std::array<unsigned char, 64000> buffer{};
    const int fd = readFd_;
    while (!cancelled_) {
        ssize_t n = read(fd, buffer.data(), buffer.size());
        if (n > 0) {
            if (live_) {
                live_->appendS16le(buffer.data(), static_cast<std::size_t>(n));
            }
            continue;
        }
        if (n == 0 || errno != EINTR) {
            break;
        }
    }
    if (live_) {
        live_->markEof();
    }
}

void PipeWireLiveVoicePipeline::asrLoop()
{
    try {
        result_ = live_ ? asr_.transcribeLive(live_->get()) : std::string();
    } catch (const std::exception& e) {
        log(std::string("live asr failed: ") + e.what());
        result_.clear();
    }
}

void PipeWireLiveVoicePipeline::stopRecorder()
{
    if (child_ != -1) {
        kill(child_, SIGINT);
    }
}

void PipeWireLiveVoicePipeline::cleanupProcess()
{
    if (child_ == -1) {
        return;
    }
    int status = 0;
    bool exited = false;
    for (int i = 0; i < 50; ++i) {
        pid_t result = waitpid(child_, &status, WNOHANG);
        if (result == child_ || result < 0) {
            exited = true;
            break;
        }
        usleep(100000);
    }
    if (!exited) {
        kill(child_, SIGTERM);
        waitpid(child_, &status, 0);
    }
    child_ = -1;
}

void PipeWireLiveVoicePipeline::closeReadFd()
{
    if (readFd_ != -1) {
        close(readFd_);
        readFd_ = -1;
    }
}

void PipeWireLiveVoicePipeline::joinThreads()
{
    if (readerThread_.joinable()) {
        readerThread_.join();
    }
    if (asrThread_.joinable()) {
        asrThread_.join();
    }
}

}  // namespace echoflow
```

- [ ] **Step 3: Wire the source into CMake**

Add `PipeWireLiveVoicePipeline.cpp` to `add_library(echoflow_service STATIC ...)` in `service/CMakeLists.txt`.

- [ ] **Step 4: Build and fix compile issues only**

Run:

```bash
cmake --build build --target echoflow-service
```

Expected: `echoflow-service` builds. If `pw-record -` is not accepted by the local PipeWire version during manual testing, change only the argument vector to the confirmed stdout form and document the exact command in the commit body.

- [ ] **Step 5: Commit Task 4**

```bash
git add service/PipeWireLiveVoicePipeline.h service/PipeWireLiveVoicePipeline.cpp service/CMakeLists.txt
git commit -m "feat(service): stream PipeWire audio into live ASR" \
  -m "Add the production live voice pipeline that captures raw PCM from pw-record, feeds qwen's live audio buffer, and joins the final ASR result on stop." \
  -m "Keep partial token display out of scope; the pipeline returns only final text to VoiceSession."
```

---

### Task 5: Wire the Production Service to the Live Pipeline

**Files:**
- Modify: `service/main.cpp`
- Test: full build and service self-test

- [ ] **Step 1: Update the service entry point**

Add the include in `service/main.cpp`:

```cpp
#include "PipeWireLiveVoicePipeline.h"
```

Replace the production interactive wiring near the bottom of `main()`:

```cpp
echoflow::AsrEngine asr(cfg);
asr.preload();
echoflow::PipeWireLiveVoicePipeline livePipeline(cfg, asr);
echoflow::Committer committer(cfg, echoflow::fcitxSocketPath(cfg));
echoflow::UnixDatagramUiNotifier ui(echoflow::uiSocketPath(cfg));
echoflow::VoiceSession session(cfg, livePipeline, committer, ui);
echoflow::Server server(cfg, session);
return server.run();
```

Remove the old `PipeWireRecorder recorder(cfg);` line from the production interactive path. Keep the `--transcribe-file` path unchanged.

- [ ] **Step 2: Build the service**

Run:

```bash
cmake --build build --target echoflow-service
```

Expected: `echoflow-service` builds.

- [ ] **Step 3: Run targeted and full tests**

Run:

```bash
./build/tests/test_voice_session
./build/tests/test_live_audio_buffer
./build/tests/test_asr_engine
ctest --test-dir build --output-on-failure
./build/service/echoflow-service --self-test
```

Expected: all tests pass; self-test reports the existing environment status without crashing.

- [ ] **Step 4: Commit Task 5**

```bash
git add service/main.cpp
git commit -m "feat(service): use live ASR for interactive recording" \
  -m "Wire the service's right-Ctrl recording path to the live PipeWire pipeline so recognition runs during capture." \
  -m "Leave --transcribe-file on the existing file-based AsrEngine path for diagnostics and benchmarks."
```

---

### Task 6: Manual Runtime Verification and Documentation Notes

**Files:**
- Modify: `docs/superpowers/specs/2026-06-19-stream-asr-default-design.md` only if implementation behavior differs from the spec.
- Modify: `docs/performance/voice-latency-optimization-report.md` only if the benchmark CLI is changed in this branch.

- [ ] **Step 1: Install and restart runtime components**

Run:

```bash
./install-user.sh
systemctl --user restart echoflow.service
systemctl --user restart echoflow-ui.service
fcitx5 -rd
```

Expected: services restart. Fcitx restart is included because the manual flow exercises the installed input-method path.

- [ ] **Step 2: Exercise tap-to-talk**

Manual steps:

```text
1. Focus a text field using Fcitx.
2. Press right Ctrl once.
3. Speak for at least 3 seconds.
4. Press right Ctrl again.
5. Confirm final text is committed and post-stop wait is shorter than the old WAV path.
6. Repeat once with focus blur during recording and confirm no text is committed.
```

- [ ] **Step 3: Inspect user service logs if behavior is wrong**

Run:

```bash
journalctl --user -u echoflow.service -n 120 --no-pager
journalctl --user -u echoflow-ui.service -n 80 --no-pager
```

Expected: no crashes. If latency remains high, capture whether delay is in `live transcription finished`, recorder shutdown, or commit.

- [ ] **Step 4: Final full verification**

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
./build/service/echoflow-service --self-test
git status --short
```

Expected: build and tests pass; `git status --short` contains only intended documentation updates, if any.

- [ ] **Step 5: Commit any verification-driven doc adjustments**

If no docs changed, skip this commit. If docs changed, run:

```bash
git add docs/superpowers/specs/2026-06-19-stream-asr-default-design.md docs/performance/voice-latency-optimization-report.md
git commit -m "docs: align live ASR notes with implementation" \
  -m "Update the design or benchmark notes to match the verified live streaming behavior."
```

---

## Plan Self-Review

- Spec coverage: Task 1 covers `VoiceSession` behavior and cancellation; Task 2 covers qwen live buffer ownership and PCM conversion; Task 3 covers qwen live ASR entry; Task 4 covers `pw-record` raw PCM and thread/process lifetime; Task 5 switches production wiring; Task 6 covers manual runtime verification.
- Placeholders: no `TBD`, `TODO`, or deferred undefined behavior remains.
- Type consistency: the plan uses `ILiveVoicePipeline`, `LiveAudioBuffer`, `AsrEngine::transcribeLive(qwen_live_audio_t*)`, and `PipeWireLiveVoicePipeline` consistently across tasks.
