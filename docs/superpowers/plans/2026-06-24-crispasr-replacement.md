# CrispASR Replacement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the qwen-asr-c ASR backend with CrispASR (subprocess + pipe) as the sole backend in the EchoFlow service.

**Architecture:** `crispasr` is an external PATH binary. `CrispAsrEngine : IAsrEngine` does one-shot file transcription (press-to-talk / `--transcribe-file`). `CrispLiveVoicePipeline : ILiveVoicePipeline` pipes `pw-record` raw PCM straight into a long-lived `crispasr --stream --stream-json` child's stdin (OS pipe, no in-process forwarding) and reads `partial`/`final` JSON events from its stdout via a pure-logic `CrispStreamAccumulator`. The qwen engine, the old segmented live pipeline, the segmenter subsystem, and the `third_party/qwen-asr` submodule are removed.

**Tech Stack:** C++17, Qt6 Test, posix_spawn, std::thread, no new third-party deps.

**Spec:** `docs/superpowers/specs/2026-06-24-crispasr-backend-design.md` (Phase 2).
**Benchmark:** `docs/performance/crispasr-benchmark.md`.

---

## File Structure

**Create:**
- `service/CrispStreamAccumulator.h` / `.cpp` — pure logic: parse one `--stream-json` line, accumulate finals/partials, expose text to emit + final text. Minimal JSON string-field extractor inside.
- `service/CrispAsrEngine.h` / `.cpp` — `IAsrEngine` impl: one-shot `crispasr -m … -f …` subprocess, returns stdout text.
- `service/CrispLiveVoicePipeline.h` / `.cpp` — `ILiveVoicePipeline` impl: pw-record → crispasr stdin pipe; parser thread reads JSON stdout → `CrispStreamAccumulator` → callback.
- `tests/test_crisp_stream_accumulator.cpp`, `tests/test_crispasr_engine.cpp` — QTest binaries.

**Modify:**
- `service/Config.h` / `Config.cpp` — add crisp fields + parse `[advanced.crisp.*]`.
- `service/main.cpp` — use `CrispAsrEngine` / `CrispLiveVoicePipeline`; drop qwen includes; extend `printDefaultConfig`.
- `service/SelfTest.cpp` — check `crispasr` on PATH + `crispModelPath` exists (replace safetensors catalog check).
- `service/CMakeLists.txt` — swap sources; drop `qwen_asr` link.
- `CMakeLists.txt` (root) — drop `qwen-asr-runtime` subdir.
- `tests/CMakeLists.txt` — swap test list.
- `tests/benchmarks/voice_latency_benchmark.cpp` — drop qwen real-engine section (or `#ifdef` out); keep fake harness.
- `.gitmodules` + submodule removal.

**Delete:**
- `service/AsrEngine.h` / `.cpp`, `service/PipeWireLiveVoicePipeline.h` / `.cpp`, `service/AudioSegmenter.h` / `.cpp`, `service/SegmentAsrWorker.h` / `.cpp`, `service/SegmentTextAccumulator.h` / `.cpp`.
- `qwen-asr-runtime/`, `third_party/qwen-asr/` (submodule).
- `tests/test_asr_engine.cpp`, `tests/test_segment_asr_worker.cpp`, `tests/test_segment_text_accumulator.cpp`, `tests/test_audio_segmenter.cpp`.

**Keep (unchanged or only lightly touched):** `Interfaces.h`, `VoiceSession.*`, `Committer.*`, `UiNotifier.*`, `Server.*`, `Recorder.*`, `SingleInstance.*`, `ModelCatalog.h` (still referenced by ui-host; harmless), `log.h`.

---

### Task 1: Config additions

**Files:** Modify `service/Config.h`, `service/Config.cpp`, `service/main.cpp`.

- [ ] **Step 1: Add fields to `Config`**

In `service/Config.h`, inside `struct Config`, after `int openBlasThreads = 4;` add:

```cpp
    std::string crispBinary = "crispasr";
    std::string crispModelPath;
    std::string crispBackend = "qwen3";
    int crispThreads = 4;
    bool crispVad = true;
    int crispFinalOnSilenceMs = 1500;
    std::string crispExtraArgs;
```

- [ ] **Step 2: Parse `[advanced.crisp.*]`**

In `service/Config.cpp` `loadDtkConf`, before the closing `}` of the `if (section == …)` chain (after the `save_live_debug_audio` branch), add:

```cpp
        } else if (section == "advanced.crisp.binary") {
            cfg.crispBinary = val;
        } else if (section == "advanced.crisp.model") {
            cfg.crispModelPath = val;
        } else if (section == "advanced.crisp.backend") {
            cfg.crispBackend = val;
        } else if (section == "advanced.crisp.threads") {
            cfg.crispThreads = std::max(1, std::stoi(val));
        } else if (section == "advanced.crisp.vad") {
            cfg.crispVad = parseBool(val);
        } else if (section == "advanced.crisp.final_on_silence_ms") {
            cfg.crispFinalOnSilenceMs = std::stoi(val);
        } else if (section == "advanced.crisp.extra_args") {
            cfg.crispExtraArgs = val;
```

Then after `cfg.modelDir = …` (end of `loadDtkConf`), default-derive the model path:

```cpp
    if (cfg.crispModelPath.empty()) {
        cfg.crispModelPath = (base / "crisp" / "qwen3-asr-0.6b-q4_k.gguf").string();
    } else {
        cfg.crispModelPath = expandPath(cfg.crispModelPath, base);
    }
```

- [ ] **Step 3: Extend `printDefaultConfig`**

In `service/main.cpp` `printDefaultConfig`, add crisp keys before the closing `}\n`:

```cpp
                 "  \"crisp_binary\": \"%s\",\n"
                 "  \"crisp_model_path\": \"%s\",\n"
                 "  \"crisp_backend\": \"%s\",\n"
                 "  \"crisp_threads\": %d,\n"
                 "  \"crisp_vad\": %s,\n"
                 "  \"crisp_final_on_silence_ms\": %d\n"
```

and add the corresponding arguments before the `);`:

```cpp
                 cfg.crispBinary.c_str(), cfg.crispModelPath.c_str(),
                 cfg.crispBackend.c_str(), cfg.crispThreads,
                 cfg.crispVad ? "true" : "false", cfg.crispFinalOnSilenceMs);
```

(Adjust the previous line's trailing comma so the JSON stays valid.)

- [ ] **Step 4: Build + commit**

```bash
cmake --build build && ctest --test-dir build --output-on-failure -R test_config
git add service/Config.h service/Config.cpp service/main.cpp
git commit -m "feat(config): add CrispASR backend config keys"
```

---

### Task 2: CrispStreamAccumulator (pure logic + JSON parse)

**Files:** Create `service/CrispStreamAccumulator.h`, `service/CrispStreamAccumulator.cpp`; Test `tests/test_crisp_stream_accumulator.cpp`.

- [ ] **Step 1: Write the failing test**

`tests/test_crisp_stream_accumulator.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>
#include "CrispStreamAccumulator.h"

using namespace echoflow;

class TestCrispStreamAccumulator : public QObject {
    Q_OBJECT
private slots:
    void partialEmitsFinalizedPlusPartial();
    void finalAccumulatesAndEmits();
    void chineseDoesNotInsertSpace();
    void malformedLineIsIgnored();
    void finalTextIncludesTrailingPartial();
};

void TestCrispStreamAccumulator::partialEmitsFinalizedPlusPartial() {
    CrispStreamAccumulator a;
    auto out = a.processEvent(R"({"type":"partial","utterance_id":1,"text":"hello","t0":0,"t1":1})");
    QVERIFY(out.has_value());
    QCOMPARE(QString::fromStdString(*out), QString("hello"));
}
void TestCrispStreamAccumulator::finalAccumulatesAndEmits() {
    CrispStreamAccumulator a;
    a.processEvent(R"({"type":"partial","utterance_id":1,"text":"hello","t0":0,"t1":1})");
    auto out = a.processEvent(R"({"type":"final","utterance_id":1,"text":"hello world","t0":0,"t1":2})");
    QVERIFY(out.has_value());
    QCOMPARE(QString::fromStdString(*out), QString("hello world"));
    QCOMPARE(QString::fromStdString(a.finalText()), QString("hello world"));
}
void TestCrispStreamAccumulator::chineseDoesNotInsertSpace() {
    CrispStreamAccumulator a;
    a.processEvent(R"({"type":"final","utterance_id":1,"text":"你好","t0":0,"t1":1})");
    auto out = a.processEvent(R"({"type":"final","utterance_id":2,"text":"世界","t0":1,"t1":2})");
    QCOMPARE(QString::fromStdString(*out), QString::fromUtf8("你好世界"));
}
void TestCrispStreamAccumulator::malformedLineIsIgnored() {
    CrispStreamAccumulator a;
    QVERIFY(!a.processEvent("not json").has_value());
    QVERIFY(!a.processEvent(R"({"type":"silence","t":1})").has_value());
}
void TestCrispStreamAccumulator::finalTextIncludesTrailingPartial() {
    CrispStreamAccumulator a;
    a.processEvent(R"({"type":"final","utterance_id":1,"text":"done","t0":0,"t1":1})");
    a.processEvent(R"({"type":"partial","utterance_id":2,"text":"wip","t0":1,"t1":2})");
    QCOMPARE(QString::fromStdString(a.finalText()), QString("done wip"));
}

QTEST_GUILESS_MAIN(TestCrispStreamAccumulator)
#include "test_crisp_stream_accumulator.moc"
```

- [ ] **Step 2: Register test, build, verify it fails**

In `tests/CMakeLists.txt`, add `test_crisp_stream_accumulator` to the `ECHOFLOW_TESTS` list. Build; expect a compile error (header missing).

- [ ] **Step 3: Write the header**

`service/CrispStreamAccumulator.h`:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_CRISP_STREAM_ACCUMULATOR_H
#define ECHOFLOW_CRISP_STREAM_ACCUMULATOR_H

#include <optional>
#include <string>
#include <string_view>

namespace echoflow {

class CrispStreamAccumulator {
public:
    // Parse one --stream-json line. Returns text to push to the partial
    // callback, or nullopt for silence/malformed/no-change.
    std::optional<std::string> processEvent(std::string_view jsonLine);
    std::string finalText() const;
    void clear();

private:
    static bool needsSpace(const std::string& prev, const std::string& next);
    std::string finalized_;
    std::string currentPartial_;
};

}  // namespace echoflow

#endif
```

- [ ] **Step 4: Write the implementation**

`service/CrispStreamAccumulator.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CrispStreamAccumulator.h"

#include <cctype>

namespace echoflow {

namespace {

std::optional<std::string> extractJsonString(std::string_view json, std::string_view field)
{
    std::string needle = "\"" + std::string(field) + "\":\"";
    auto pos = json.find(needle);
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }
    pos += needle.size();
    std::string out;
    while (pos < json.size()) {
        char c = json[pos];
        if (c == '"') {
            return out;
        }
        if (c == '\\' && pos + 1 < json.size()) {
            char e = json[pos + 1];
            pos += 2;
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    if (pos + 4 <= json.size()) {
                        auto hex = std::string(json.substr(pos, 4));
                        try {
                            unsigned cp = static_cast<unsigned>(std::stoul(hex, nullptr, 16));
                            if (cp < 0x80) {
                                out += static_cast<char>(cp);
                            } else if (cp < 0x800) {
                                out += static_cast<char>(0xC0 | (cp >> 6));
                                out += static_cast<char>(0x80 | (cp & 0x3F));
                            } else {
                                out += static_cast<char>(0xE0 | (cp >> 12));
                                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                                out += static_cast<char>(0x80 | (cp & 0x3F));
                            }
                            pos += 4;
                        } catch (...) {
                            out += e;
                        }
                    } else {
                        out += e;
                    }
                    break;
                }
                default: out += e; break;
            }
        } else {
            out += c;
            ++pos;
        }
    }
    return out;
}

bool isAsciiWordByte(unsigned char b)
{
    return std::isalnum(b) || b == '_' || b == '-' || b == '\'';
}

}  // namespace

bool CrispStreamAccumulator::needsSpace(const std::string& prev, const std::string& next)
{
    if (prev.empty() || next.empty()) {
        return false;
    }
    unsigned char last = static_cast<unsigned char>(prev.back());
    unsigned char first = static_cast<unsigned char>(next.front());
    return isAsciiWordByte(last) && isAsciiWordByte(first);
}

std::optional<std::string> CrispStreamAccumulator::processEvent(std::string_view jsonLine)
{
    auto type = extractJsonString(jsonLine, "type");
    if (!type) {
        return std::nullopt;
    }
    if (*type == "partial") {
        auto text = extractJsonString(jsonLine, "text");
        currentPartial_ = text.value_or("");
        std::string out = finalized_;
        if (needsSpace(out, currentPartial_)) {
            out += " ";
        }
        out += currentPartial_;
        return out;
    }
    if (*type == "final") {
        auto text = extractJsonString(jsonLine, "text");
        currentPartial_.clear();
        std::string piece = text.value_or("");
        if (needsSpace(finalized_, piece)) {
            finalized_ += " ";
        }
        finalized_ += piece;
        return finalized_;
    }
    return std::nullopt;  // silence / unknown
}

std::string CrispStreamAccumulator::finalText() const
{
    std::string out = finalized_;
    if (!currentPartial_.empty()) {
        if (needsSpace(out, currentPartial_)) {
            out += " ";
        }
        out += currentPartial_;
    }
    return out;
}

void CrispStreamAccumulator::clear()
{
    finalized_.clear();
    currentPartial_.clear();
}

}  // namespace echoflow
```

- [ ] **Step 5: Build, run test, commit**

```bash
cmake --build build && ctest --test-dir build --output-on-failure -R test_crisp_stream_accumulator
git add service/CrispStreamAccumulator.h service/CrispStreamAccumulator.cpp tests/test_crisp_stream_accumulator.cpp tests/CMakeLists.txt
git commit -m "feat(crisp): add CrispStreamAccumulator for --stream-json events"
```

---

### Task 3: CrispAsrEngine (one-shot IAsrEngine)

**Files:** Create `service/CrispAsrEngine.h`, `service/CrispAsrEngine.cpp`; Test `tests/test_crispasr_engine.cpp`.

- [ ] **Step 1: Write the failing test**

`tests/test_crispasr_engine.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>
#include "CrispAsrEngine.h"

using namespace echoflow;

class TestCrispAsrEngine : public QObject {
    Q_OBJECT
private slots:
    void transcribeReturnsEmptyWhenBinaryMissing();
    void transcribeReturnsEmptyWhenModelMissing();
};

void TestCrispAsrEngine::transcribeReturnsEmptyWhenBinaryMissing() {
    Config cfg = Config::defaultConfig();
    cfg.crispBinary = "/tmp/echoflow-no-such-crispasr-binary";
    cfg.crispModelPath = "/tmp/echoflow-no-such-model.gguf";
    CrispAsrEngine engine(cfg);
    try {
        QCOMPARE(QString::fromStdString(engine.transcribe("/tmp/echoflow-no-such-audio.wav")),
                 QString());
    } catch (const std::exception& e) {
        QFAIL(qPrintable(QStringLiteral("transcribe threw: %1").arg(e.what())));
    }
}
void TestCrispAsrEngine::transcribeReturnsEmptyWhenModelMissing() {
    Config cfg = Config::defaultConfig();
    cfg.crispBinary = "crispasr";           // may or may not be present
    cfg.crispModelPath = "/tmp/echoflow-no-such-model.gguf";
    CrispAsrEngine engine(cfg);
    QVERIFY(engine.transcribe("/tmp/echoflow-no-such-audio.wav").empty());
}

QTEST_GUILESS_MAIN(TestCrispAsrEngine)
#include "test_crispasr_engine.moc"
```

- [ ] **Step 2: Register test, build, verify it fails**

Add `test_crispasr_engine` to `ECHOFLOW_TESTS` in `tests/CMakeLists.txt`. Build; expect missing-header error.

- [ ] **Step 3: Write the header**

`service/CrispAsrEngine.h`:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_CRISP_ASR_ENGINE_H
#define ECHOFLOW_CRISP_ASR_ENGINE_H

#include "Config.h"
#include "Interfaces.h"

#include <string>
#include <vector>

namespace echoflow {

class CrispAsrEngine : public IAsrEngine {
public:
    explicit CrispAsrEngine(Config cfg);
    std::string transcribe(const std::filesystem::path& audio) override;

    static std::vector<std::string> buildArgs(const Config& cfg,
                                              const std::filesystem::path& audio);

private:
    Config cfg_;
};

}  // namespace echoflow

#endif
```

- [ ] **Step 4: Write the implementation**

`service/CrispAsrEngine.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CrispAsrEngine.h"

#include "log.h"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace echoflow {

namespace {

std::string languageCode(const std::string& value)
{
    if (value == "Chinese" || value == "chinese" || value == "zh" || value == "cmn") {
        return "zh";
    }
    if (value == "English" || value == "english" || value == "en") {
        return "en";
    }
    if (value.empty()) {
        return {};
    }
    return value;  // already an ISO code (ja, ko, ...)
}

}  // namespace

std::vector<std::string> CrispAsrEngine::buildArgs(const Config& cfg,
                                                   const std::filesystem::path& audio)
{
    std::vector<std::string> args = {
        cfg.crispBinary,
        "-m", cfg.crispModelPath,
        "--backend", cfg.crispBackend,
        "-f", audio.string(),
        "-t", std::to_string(cfg.crispThreads),
    };
    auto lang = languageCode(cfg.language.value_or(""));
    if (!lang.empty()) {
        args.push_back("-l");
        args.push_back(lang);
    }
    if (!cfg.crispExtraArgs.empty()) {
        args.push_back(cfg.crispExtraArgs);
    }
    return args;
}

CrispAsrEngine::CrispAsrEngine(Config cfg)
    : cfg_(std::move(cfg))
{
}

std::string CrispAsrEngine::transcribe(const std::filesystem::path& audio)
{
    auto args = buildArgs(cfg_, audio);

    int outPipe[2] = {-1, -1};
    if (pipe(outPipe) != 0) {
        log(std::string("crispasr pipe failed: ") + std::strerror(errno));
        return {};
    }

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(outPipe[0]);
        close(outPipe[1]);
        return {};
    }
    posix_spawn_file_actions_adddup2(&actions, outPipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, outPipe[0]);
    posix_spawn_file_actions_addclose(&actions, outPipe[1]);

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (auto& a : args) {
        argv.push_back(a.data());
    }
    argv.push_back(nullptr);

    pid_t pid = -1;
    int rc = posix_spawnp(&pid, cfg_.crispBinary.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(outPipe[1]);
    if (rc != 0) {
        log(std::string("posix_spawnp crispasr failed: ") + std::strerror(rc));
        close(outPipe[0]);
        return {};
    }

    std::string output;
    std::array<char, 4096> buf{};
    ssize_t n = 0;
    while ((n = read(outPipe[0], buf.data(), buf.size())) > 0) {
        output.append(buf.data(), static_cast<size_t>(n));
    }
    close(outPipe[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {
        // retry
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        log("crispasr exited non-zero; partial output: " + output);
    }

    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }
    return output;
}

}  // namespace echoflow
```

- [ ] **Step 5: Build, run test, commit**

```bash
cmake --build build && ctest --test-dir build --output-on-failure -R test_crispasr_engine
git add service/CrispAsrEngine.h service/CrispAsrEngine.cpp tests/test_crispasr_engine.cpp tests/CMakeLists.txt
git commit -m "feat(crisp): add CrispAsrEngine one-shot transcription backend"
```

---

### Task 4: CrispLiveVoicePipeline

**Files:** Create `service/CrispLiveVoicePipeline.h`, `service/CrispLiveVoicePipeline.cpp`.

- [ ] **Step 1: Write the header**

`service/CrispLiveVoicePipeline.h`:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_CRISP_LIVE_VOICE_PIPELINE_H
#define ECHOFLOW_CRISP_LIVE_VOICE_PIPELINE_H

#include "CrispStreamAccumulator.h"
#include "Config.h"
#include "Interfaces.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <sys/types.h>
#include <thread>

namespace echoflow {

class CrispLiveVoicePipeline : public ILiveVoicePipeline {
public:
    explicit CrispLiveVoicePipeline(Config cfg);
    ~CrispLiveVoicePipeline() override;

    CrispLiveVoicePipeline(const CrispLiveVoicePipeline&) = delete;
    CrispLiveVoicePipeline& operator=(const CrispLiveVoicePipeline&) = delete;

    void start() override;
    std::string finish() override;
    void cancel() override;
    void setPartialTextCallback(std::function<void(const std::string&)> callback) override;

    static std::vector<std::string> buildCrispArgs(const Config& cfg);

private:
    void parserLoop();
    void stopRecorder();
    void reapChild(pid_t& child);
    void emitText(const std::string& text);

    Config cfg_;
    pid_t recorderChild_ = -1;
    pid_t crispChild_ = -1;
    int crispOutFd_ = -1;
    std::thread parserThread_;
    std::function<void(const std::string&)> partialTextCallback_;
    mutable std::mutex callbackMutex_;
    CrispStreamAccumulator accumulator_;
    mutable std::mutex accumulatorMutex_;
    std::atomic<bool> active_{false};
    std::atomic<bool> cancelled_{false};
    std::chrono::steady_clock::time_point startedAt_{};
};

}  // namespace echoflow

#endif
```

- [ ] **Step 2: Write the implementation**

`service/CrispLiveVoicePipeline.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CrispLiveVoicePipeline.h"

#include "Recorder.h"
#include "log.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace echoflow {

using Clock = std::chrono::steady_clock;

namespace {

double elapsedSeconds(Clock::time_point started)
{
    return std::chrono::duration<double>(Clock::now() - started).count();
}

bool waitForChild(pid_t child, int attempts)
{
    int status = 0;
    for (int i = 0; i < attempts; ++i) {
        pid_t r = waitpid(child, &status, WNOHANG);
        if (r == child) {
            return true;
        }
        if (r < 0 && errno != EINTR) {
            return true;
        }
        usleep(100000);
    }
    return false;
}

std::string languageCode(const std::string& value)
{
    if (value == "Chinese" || value == "chinese" || value == "zh") {
        return "zh";
    }
    if (value == "English" || value == "english" || value == "en") {
        return "en";
    }
    return value;
}

}  // namespace

std::vector<std::string> CrispLiveVoicePipeline::buildCrispArgs(const Config& cfg)
{
    std::vector<std::string> args = {
        cfg.crispBinary,
        "--stream",
        "--stream-json",
        "-m", cfg.crispModelPath,
        "--backend", cfg.crispBackend,
        "--stream-final-mode", "redecode",
        "--stream-final-on-silence-ms", std::to_string(cfg.crispFinalOnSilenceMs),
        "-t", std::to_string(cfg.crispThreads),
    };
    if (cfg.crispVad) {
        args.push_back("--vad");
    }
    auto lang = languageCode(cfg.language.value_or(""));
    if (!lang.empty()) {
        args.push_back("-l");
        args.push_back(lang);
    }
    if (!cfg.crispExtraArgs.empty()) {
        args.push_back(cfg.crispExtraArgs);
    }
    return args;
}

CrispLiveVoicePipeline::CrispLiveVoicePipeline(Config cfg)
    : cfg_(std::move(cfg))
{
}

CrispLiveVoicePipeline::~CrispLiveVoicePipeline()
{
    cancel();
}

void CrispLiveVoicePipeline::setPartialTextCallback(
    std::function<void(const std::string&)> callback)
{
    std::lock_guard<std::mutex> lock(callbackMutex_);
    partialTextCallback_ = std::move(callback);
}

void CrispLiveVoicePipeline::start()
{
    if (active_) {
        return;
    }

    int pwPipe[2] = {-1, -1};   // pw-record stdout -> crispasr stdin
    int outPipe[2] = {-1, -1};  // crispasr stdout -> parent parser
    if (pipe(pwPipe) != 0 || pipe(outPipe) != 0) {
        throw std::runtime_error(std::string("pipe failed: ") + std::strerror(errno));
    }

    auto closeBoth = [](int p[2]) { if (p[0] != -1) close(p[0]); if (p[1] != -1) close(p[1]); };

    // --- spawn pw-record (stdout -> pwPipe[1]) ---
    posix_spawn_file_actions_t ra;
    if (posix_spawn_file_actions_init(&ra) != 0) {
        closeBoth(pwPipe); closeBoth(outPipe);
        throw std::runtime_error("pw-record spawn actions init failed");
    }
    posix_spawn_file_actions_adddup2(&ra, pwPipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&ra, pwPipe[0]);
    posix_spawn_file_actions_addclose(&ra, outPipe[0]);
    posix_spawn_file_actions_addclose(&ra, outPipe[1]);

    auto recArgs = buildPipeWireLiveRecordArgs(cfg_);
    std::vector<char*> recArgv;
    recArgv.reserve(recArgs.size() + 1);
    for (auto& a : recArgs) {
        recArgv.push_back(a.data());
    }
    recArgv.push_back(nullptr);

    pid_t recPid = -1;
    int rc = posix_spawnp(&recPid, "pw-record", &ra, nullptr, recArgv.data(), environ);
    posix_spawn_file_actions_destroy(&ra);
    if (rc != 0) {
        closeBoth(pwPipe); closeBoth(outPipe);
        throw std::runtime_error(std::string("posix_spawnp pw-record failed: ") + std::strerror(rc));
    }
    close(pwPipe[1]);  // parent does not write to crispasr stdin

    // --- spawn crispasr (stdin <- pwPipe[0], stdout -> outPipe[1]) ---
    posix_spawn_file_actions_t ca;
    if (posix_spawn_file_actions_init(&ca) != 0) {
        kill(recPid, SIGTERM);
        waitForChild(recPid, 20);
        close(pwPipe[0]); closeBoth(outPipe);
        throw std::runtime_error("crispasr spawn actions init failed");
    }
    posix_spawn_file_actions_adddup2(&ca, pwPipe[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&ca, outPipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&ca, outPipe[0]);

    auto crispArgs = buildCrispArgs(cfg_);
    std::vector<char*> crispArgv;
    crispArgv.reserve(crispArgs.size() + 1);
    for (auto& a : crispArgs) {
        crispArgv.push_back(a.data());
    }
    crispArgv.push_back(nullptr);

    pid_t crispPid = -1;
    rc = posix_spawnp(&crispPid, cfg_.crispBinary.c_str(), &ca, nullptr, crispArgv.data(), environ);
    posix_spawn_file_actions_destroy(&ca);
    close(pwPipe[0]);
    close(outPipe[1]);
    if (rc != 0) {
        kill(recPid, SIGTERM);
        waitForChild(recPid, 20);
        close(outPipe[0]);
        throw std::runtime_error(std::string("posix_spawnp crispasr failed: ") + std::strerror(rc));
    }

    recorderChild_ = recPid;
    crispChild_ = crispPid;
    crispOutFd_ = outPipe[0];
    accumulator_.clear();
    cancelled_ = false;
    startedAt_ = Clock::now();
    active_ = true;

    parserThread_ = std::thread(&CrispLiveVoicePipeline::parserLoop, this);
    log("crisp live pipeline started");
}

void CrispLiveVoicePipeline::parserLoop()
{
    try {
        std::array<char, 8192> buf{};
        std::string pending;
        int fd = crispOutFd_;
        while (fd != -1 && !cancelled_) {
            ssize_t n = read(fd, buf.data(), buf.size());
            if (n > 0) {
                pending.append(buf.data(), static_cast<size_t>(n));
                size_t pos = 0;
                while (true) {
                    auto nl = pending.find('\n', pos);
                    if (nl == std::string::npos) {
                        break;
                    }
                    std::string line = pending.substr(pos, nl - pos);
                    pos = nl + 1;
                    std::optional<std::string> emitted;
                    {
                        std::lock_guard<std::mutex> lock(accumulatorMutex_);
                        emitted = accumulator_.processEvent(line);
                    }
                    if (emitted) {
                        emitText(*emitted);
                    }
                }
                pending.erase(0, pos);
                continue;
            }
            if (n == 0) {
                break;  // EOF: crispasr exited
            }
            if (errno == EINTR) {
                continue;
            }
            log(std::string("crispasr stdout read failed: ") + std::strerror(errno));
            break;
        }
    } catch (const std::exception& e) {
        log(std::string("crisp parser thread failed: ") + e.what());
    } catch (...) {
        log("crisp parser thread failed");
    }
}

void CrispLiveVoicePipeline::emitText(const std::string& text)
{
    std::function<void(const std::string&)> cb;
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        cb = partialTextCallback_;
    }
    if (cb) {
        cb(text);
    }
}

std::string CrispLiveVoicePipeline::finish()
{
    if (!active_) {
        return {};
    }
    auto finishStarted = Clock::now();
    log("crisp live pipeline stop requested after " + std::to_string(elapsedSeconds(startedAt_)) + "s");

    stopRecorder();  // SIGINT pw-record -> its stdout closes -> crispasr stdin EOF
    reapChild(recorderChild_);
    // crispasr flushes trailing finals on stdin EOF, then exits -> parser EOF
    if (parserThread_.joinable()) {
        parserThread_.join();
    }
    reapChild(crispChild_);
    if (crispOutFd_ != -1) {
        close(crispOutFd_);
        crispOutFd_ = -1;
    }

    active_ = false;
    std::string finalText;
    if (!cancelled_) {
        std::lock_guard<std::mutex> lock(accumulatorMutex_);
        finalText = accumulator_.finalText();
    }
    log("crisp live pipeline finish returned in " + std::to_string(elapsedSeconds(finishStarted)) +
        "s, chars=" + std::to_string(finalText.size()));
    return finalText;
}

void CrispLiveVoicePipeline::cancel()
{
    try {
        cancelled_ = true;
        stopRecorder();
        reapChild(recorderChild_);
        if (crispChild_ != -1) {
            kill(crispChild_, SIGTERM);
        }
        if (parserThread_.joinable()) {
            parserThread_.join();
        }
        reapChild(crispChild_);
        if (crispOutFd_ != -1) {
            close(crispOutFd_);
            crispOutFd_ = -1;
        }
        active_ = false;
        std::lock_guard<std::mutex> lock(accumulatorMutex_);
        accumulator_.clear();
    } catch (const std::exception& e) {
        log(std::string("crisp live pipeline cancel failed: ") + e.what());
    } catch (...) {
        log("crisp live pipeline cancel failed");
    }
}

void CrispLiveVoicePipeline::stopRecorder()
{
    if (recorderChild_ != -1) {
        kill(recorderChild_, SIGINT);
    }
}

void CrispLiveVoicePipeline::reapChild(pid_t& child)
{
    if (child == -1) {
        return;
    }
    pid_t c = child;
    bool exited = waitForChild(c, 50);
    if (!exited) {
        kill(c, SIGTERM);
        exited = waitForChild(c, 20);
    }
    if (!exited) {
        kill(c, SIGKILL);
        waitForChild(c, 20);
    }
    child = -1;
}

}  // namespace echoflow
```

- [ ] **Step 3: Build (will fail at link until main.cpp wired — that's fine, just confirm compile of the .cpp)**

```bash
cmake --build build 2>&1 | rg -i 'error' | head
```

- [ ] **Step 4: Commit**

```bash
git add service/CrispLiveVoicePipeline.h service/CrispLiveVoicePipeline.cpp
git commit -m "feat(crisp): add CrispLiveVoicePipeline native streaming backend"
```

---

### Task 5: Wire main.cpp to CrispASR

**Files:** Modify `service/main.cpp`.

- [ ] **Step 1: Swap includes + construction**

Replace the qwen includes/usage. In `service/main.cpp`:

Replace `#include "AsrEngine.h"` with `#include "CrispAsrEngine.h"`, and `#include "PipeWireLiveVoicePipeline.h"` with `#include "CrispLiveVoicePipeline.h"`.

In the `--transcribe-file` block, replace:
```cpp
        echoflow::AsrEngine asr(cfg);
```
with:
```cpp
        echoflow::CrispAsrEngine asr(cfg);
```

In the daemon block, replace:
```cpp
    echoflow::AsrEngine asr(cfg);
    asr.preload();
    echoflow::Committer committer(cfg, echoflow::fcitxSocketPath(cfg));
    echoflow::UnixDatagramUiNotifier ui(echoflow::uiSocketPath(cfg));
    if (cfg.streamTranscription) {
        echoflow::PipeWireLiveVoicePipeline livePipeline(cfg, asr);
```
with:
```cpp
    echoflow::Committer committer(cfg, echoflow::fcitxSocketPath(cfg));
    echoflow::UnixDatagramUiNotifier ui(echoflow::uiSocketPath(cfg));
    if (cfg.streamTranscription) {
        echoflow::CrispLiveVoicePipeline livePipeline(cfg);
```

(The press-to-talk branch keeps `recorder` + `asr` where `asr` is now `CrispAsrEngine` — declare it there: replace `echoflow::PipeWireRecorder recorder(cfg);` block to also construct `echoflow::CrispAsrEngine asr(cfg);` before `VoiceSession session(cfg, recorder, asr, committer, ui);`.)

- [ ] **Step 2: Build**

```bash
cmake --build build
```

Expect: link errors from still-present qwen code? No — AsrEngine/PipeWireLiveVoicePipeline still compile (still in CMake). It should build. (Their removal is Task 7.)

- [ ] **Step 3: Smoke test transcribe-file**

```bash
./build/service/echoflow-service --print-default-config | rg crisp
GGUF=~/.config/echoflow/crisp/qwen3-asr-0.6b-q4_k.gguf
./build/service/echoflow-service --transcribe-file third_party/qwen-asr/samples/jfk.wav
```

Expected: prints the CrispASR transcript (`And so, my fellow Americans…`).

- [ ] **Step 4: Commit**

```bash
git add service/main.cpp
git commit -m "feat(service): use CrispASR as the ASR backend"
```

---

### Task 6: SelfTest for crispasr

**Files:** Modify `service/SelfTest.cpp`.

- [ ] **Step 1: Replace the model check with crispasr checks**

In `service/SelfTest.cpp`, replace the `modelDetail`/`modelOk` block (lines ~54-66) and the `"model available"` check with:

```cpp
    std::string modelPath = cfg.crispModelPath;
    bool modelOk = !modelPath.empty() && fs::exists(modelPath);
    std::string modelDetail = modelOk ? modelPath
                                      : (modelPath.empty() ? "crisp model path not set"
                                                           : "missing: " + modelPath);

    std::string crispBinCmd = "command -v " + cfg.crispBinary + " >/dev/null 2>&1";

    return {
        {"recordings dir can be created", canCreateDirectory(cfg.recordingsDir), cfg.recordingsDir},
        {"pw-record available", std::system("command -v pw-record >/dev/null 2>&1") == 0, "pw-record"},
        {"crispasr available", std::system(crispBinCmd.c_str()) == 0, cfg.crispBinary},
        {"crisp model available", modelOk, modelDetail},
        {"control socket path parent", fs::exists(controlSocketPath(cfg).parent_path()),
         controlSocketPath(cfg).string()},
        {"fcitx socket path parent", fs::exists(fcitxSocketPath(cfg).parent_path()),
         fcitxSocketPath(cfg).string()},
        {"ui socket path parent", fs::exists(uiSocketPath(cfg).parent_path()),
         uiSocketPath(cfg).string()},
    };
```

Remove the now-unused `findModel`/`missingModelFiles` usage; the `#include "ModelCatalog.h"` can stay (harmless) or be removed.

- [ ] **Step 2: Build + run self-test**

```bash
cmake --build build && ./build/service/echoflow-service --self-test
```

Expected: `[OK] crispasr available` and `[OK] crisp model available`.

- [ ] **Step 3: Commit**

```bash
git add service/SelfTest.cpp
git commit -m "feat(selftest): check crispasr binary and GGUF model"
```

---

### Task 7: Remove qwen-asr-c + dead segmenter code

**Files:** Delete files; modify `service/CMakeLists.txt`, root `CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/benchmarks/voice_latency_benchmark.cpp`; remove submodule.

- [ ] **Step 1: Find any remaining references**

```bash
rg -n 'AsrEngine|PipeWireLiveVoicePipeline|AudioSegmenter|SegmentAsrWorker|SegmentTextAccumulator|qwen_asr|qwen-asr' service tests ui-host fcitx-addon CMakeLists.txt qwen-asr-runtime 2>/dev/null
```

Confirm only the files slated for deletion/ed (plus `ui-host` ModelCatalog usage, which is untouched).

- [ ] **Step 2: Delete service sources**

```bash
git rm service/AsrEngine.h service/AsrEngine.cpp \
       service/PipeWireLiveVoicePipeline.h service/PipeWireLiveVoicePipeline.cpp \
       service/AudioSegmenter.h service/AudioSegmenter.cpp \
       service/SegmentAsrWorker.h service/SegmentAsrWorker.cpp \
       service/SegmentTextAccumulator.h service/SegmentTextAccumulator.cpp
```

- [ ] **Step 3: Update `service/CMakeLists.txt`**

Replace the source list and link line:

```cmake
add_library(echoflow_service STATIC
    Config.cpp
    CrispAsrEngine.cpp
    CrispLiveVoicePipeline.cpp
    CrispStreamAccumulator.cpp
    Committer.cpp
    Recorder.cpp
    SelfTest.cpp
    Server.cpp
    SingleInstance.cpp
    UiNotifier.cpp
    VoiceSession.cpp)

target_include_directories(echoflow_service PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
```

(Remove the `target_link_libraries(echoflow_service PUBLIC qwen_asr)` line entirely.)

- [ ] **Step 4: Remove qwen-asr-runtime + submodule**

In root `CMakeLists.txt`, remove the `add_subdirectory(qwen-asr-runtime)` line.

```bash
git rm -r qwen-asr-runtime
git submodule deinit -f third_party/qwen-asr
git rm -f third_party/qwen-asr
# remove the submodule entry from .gitmodules and .git/config
git config -f .gitmodules --remove-section submodule.third_party/qwen-asr
git add .gitmodules
```

- [ ] **Step 5: Update `tests/CMakeLists.txt`**

Set `ECHOFLOW_TESTS` to:

```cmake
set(ECHOFLOW_TESTS
    test_config
    test_crispasr_engine
    test_crisp_stream_accumulator
    test_voice_session
    test_committer
    test_selftest
    test_model_catalog
    test_recorder_command
    test_single_instance)
```

```bash
git rm tests/test_asr_engine.cpp tests/test_segment_asr_worker.cpp \
       tests/test_segment_text_accumulator.cpp tests/test_audio_segmenter.cpp
```

- [ ] **Step 6: Fix the benchmark**

In `tests/benchmarks/voice_latency_benchmark.cpp`, remove the `#include "AsrEngine.h"`, the real-engine block that constructs `AsrEngine` (and any `qwen_asr_*` include), leaving the fake `TimedRecorder`/`TimedAsr` harness. If too entangled, replace the real-engine section's body with `// Real CrispASR engine benchmarking is done via the crispasr CLI; see docs/performance/crispasr-benchmark.md.` and remove the unused includes. Ensure it compiles standalone.

- [ ] **Step 7: Reconfigure and build**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "refactor(service): remove qwen-asr-c and segmenter subsystem

CrispASR fully replaces the antirez qwen-asr-c runtime. Drops the
qwen-asr-runtime + third_party/qwen-asr submodule, the AsrEngine,
PipeWireLiveVoicePipeline, and the now-unused AudioSegmenter /
SegmentAsrWorker / SegmentTextAccumulator, along with their tests."
```

---

### Task 8: Full verification

- [ ] **Step 1: Full test suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: all tests PASS (spec_tests included).

- [ ] **Step 2: Sanity shell scripts**

```bash
bash -n install-user.sh uninstall-user.sh tests/spec/*.sh
sh -n run.sh
```

- [ ] **Step 3: Manual live smoke**

```bash
# ensure crispasr is on PATH for the running service
ln -sf ~/projects/CrispASR/build/bin/crispasr ~/.local/bin/crispasr  # if not installed
./run.sh
# press right Ctrl, speak a Chinese sentence, release; verify text is committed via Fcitx
```

- [ ] **Step 4: Final commit (any verification fixes)**

```bash
git add -A
git commit -m "test(crisp): verification fixes from full build and smoke test" || echo "nothing to commit"
git log --oneline -10
```

---

## Self-Review

**Spec coverage:** Phase 2 of the spec → CrispAsrEngine (Task 3), CrispLiveVoicePipeline (Task 4, redecode + ~1500ms, native streaming), config keys (Task 1), SelfTest (Task 6), JSON parser isolated in CrispStreamAccumulator (Task 2), qwen removal (Task 7), testing without real binary/weights (Tasks 2-3 tests use missing paths). Streaming LID-hang avoided via `-l` mapping. All covered.

**Type consistency:** `IAsrEngine::transcribe(path)`, `ILiveVoicePipeline::{start,finish,cancel,setPartialTextCallback}` signatures match across header/impl/main. `CrispStreamAccumulator::processEvent` returns `optional<string>` consistently. `buildCrispArgs`/`buildArgs` names distinct per class.

**Placeholder scan:** All code blocks are complete; no TBD/TODO. The only judgment call is Task 7 Step 6 (benchmark) — if the real-engine section is too entangled to trivially edit, the step gives the fallback (comment-out + doc pointer). Verified against the file list.
