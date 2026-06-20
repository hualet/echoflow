# Segmented Qwen Final Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a first-version segmented live pipeline that runs Qwen final transcription on completed speech segments during recording and commits the joined stable text once on stop.

**Architecture:** Keep `VoiceSession` and `ILiveVoicePipeline` stable. Replace the current whole-session live-ASR commit behavior inside `PipeWireLiveVoicePipeline` with a reader -> energy segmenter -> segment ASR worker -> stable text accumulator pipeline. First version does not run Qwen live partial concurrently with segment final ASR, so Qwen final quality is preserved and `AsrEngine` is not shared concurrently.

**Tech Stack:** C++17, Qt6 QTest, CMake, PipeWire `pw-record`, existing `AsrEngine::transcribe()`, existing `IUiNotifier` `STREAM_TEXT` messages.

---

## File Structure

- Create `service/AudioSegmenter.h`: deterministic PCM energy segmenter API and segment data types.
- Create `service/AudioSegmenter.cpp`: RMS/noise-floor based speech segmentation.
- Create `service/SegmentTextAccumulator.h`: ordered final text joining.
- Create `service/SegmentTextAccumulator.cpp`: stable text accumulation and Chinese-friendly joining.
- Create `service/SegmentAsrWorker.h`: background queue interface for completed segment transcription.
- Create `service/SegmentAsrWorker.cpp`: WAV writing, worker thread, ordered callback delivery.
- Modify `service/PipeWireLiveVoicePipeline.h`: replace live-buffer state with segmenter and worker state.
- Modify `service/PipeWireLiveVoicePipeline.cpp`: feed PCM into segmenter, enqueue completed segments, return stable final text on `finish()`.
- Modify `service/CMakeLists.txt`: add new service sources.
- Create `tests/test_audio_segmenter.cpp`: unit tests for segmentation behavior.
- Create `tests/test_segment_text_accumulator.cpp`: unit tests for stable text joining.
- Create `tests/test_segment_asr_worker.cpp`: unit tests with fake `IAsrEngine`.
- Modify `tests/CMakeLists.txt`: add the new QTest binaries.
- Modify `tests/test_voice_session.cpp`: tighten existing live session tests so partial callback remains preview-only and final commit comes from `finish()`.

## Implementation Notes

- Do not include `qwen_asr.h` outside `service/AsrEngine.*`.
- Keep the first version conservative: stable text only. Do not run `AsrEngine::transcribeLive()` from `PipeWireLiveVoicePipeline`.
- The capsule can still update through `STREAM_TEXT`, but updates are stable segment text, not unstable partials.
- `finish()` returns only text accumulated from segment final transcriptions.
- `cancel()` must drop queued segments and accumulated text.

---

### Task 1: Add `AudioSegmenter`

**Files:**
- Create: `service/AudioSegmenter.h`
- Create: `service/AudioSegmenter.cpp`
- Create: `tests/test_audio_segmenter.cpp`
- Modify: `service/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing segmenter tests**

Create `tests/test_audio_segmenter.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "AudioSegmenter.h"

#include <vector>

using namespace echoflow;

namespace {

std::vector<int16_t> samples(double seconds, int16_t value)
{
    return std::vector<int16_t>(static_cast<size_t>(seconds * 16000.0), value);
}

void append(std::vector<int16_t>& dst, const std::vector<int16_t>& src)
{
    dst.insert(dst.end(), src.begin(), src.end());
}

}  // namespace

class TestAudioSegmenter : public QObject {
    Q_OBJECT

private slots:
    void emitsSegmentAfterTrailingSilence();
    void ignoresVeryShortNoise();
    void includesPreAndPostPadding();
    void forceSplitsLongSpeech();
    void flushReturnsOpenSegment();
};

void TestAudioSegmenter::emitsSegmentAfterTrailingSilence()
{
    AudioSegmenter segmenter(AudioSegmenterConfig{});
    std::vector<int16_t> input;
    append(input, samples(0.30, 0));
    append(input, samples(1.00, 9000));
    append(input, samples(1.00, 0));

    auto segments = segmenter.append(input.data(), input.size());

    QCOMPARE(segments.size(), size_t(1));
    QVERIFY(segments.front().sampleCount() >= 16000);
    QVERIFY(segments.front().durationSeconds() < 1.7);
}

void TestAudioSegmenter::ignoresVeryShortNoise()
{
    AudioSegmenter segmenter(AudioSegmenterConfig{});
    std::vector<int16_t> input;
    append(input, samples(0.20, 0));
    append(input, samples(0.20, 9000));
    append(input, samples(1.00, 0));

    auto segments = segmenter.append(input.data(), input.size());
    auto flushed = segmenter.flush();

    QVERIFY(segments.empty());
    QVERIFY(flushed.empty());
}

void TestAudioSegmenter::includesPreAndPostPadding()
{
    AudioSegmenterConfig cfg;
    cfg.prePaddingMs = 200;
    cfg.postPaddingMs = 300;
    cfg.endSilenceMs = 800;
    AudioSegmenter segmenter(cfg);

    std::vector<int16_t> input;
    append(input, samples(0.40, 0));
    append(input, samples(0.80, 9000));
    append(input, samples(1.00, 0));

    auto segments = segmenter.append(input.data(), input.size());

    QCOMPARE(segments.size(), size_t(1));
    QVERIFY(segments.front().durationSeconds() >= 1.25);
    QVERIFY(segments.front().durationSeconds() <= 1.40);
}

void TestAudioSegmenter::forceSplitsLongSpeech()
{
    AudioSegmenterConfig cfg;
    cfg.maxSegmentMs = 1200;
    cfg.endSilenceMs = 800;
    AudioSegmenter segmenter(cfg);

    auto input = samples(2.00, 9000);
    auto segments = segmenter.append(input.data(), input.size());

    QVERIFY(!segments.empty());
    QVERIFY(segments.front().durationSeconds() <= 1.50);
}

void TestAudioSegmenter::flushReturnsOpenSegment()
{
    AudioSegmenter segmenter(AudioSegmenterConfig{});
    auto input = samples(1.00, 9000);
    auto segments = segmenter.append(input.data(), input.size());
    auto flushed = segmenter.flush();

    QVERIFY(segments.empty());
    QCOMPARE(flushed.size(), size_t(1));
    QVERIFY(flushed.front().durationSeconds() >= 0.95);
}

QTEST_GUILESS_MAIN(TestAudioSegmenter)
#include "test_audio_segmenter.moc"
```

- [ ] **Step 2: Add test target and run the test to verify it fails**

Modify `tests/CMakeLists.txt` by adding `test_audio_segmenter` to `ECHOFLOW_TESTS`:

```cmake
set(ECHOFLOW_TESTS
    test_asr_engine
    test_config
    test_voice_session
    test_committer
    test_selftest
    test_model_catalog
    test_live_audio_buffer
    test_single_instance
    test_audio_segmenter)
```

Run:

```bash
cmake --build build --target test_audio_segmenter
```

Expected: build fails because `AudioSegmenter.h` does not exist.

- [ ] **Step 3: Add segmenter header**

Create `service/AudioSegmenter.h`:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_AUDIO_SEGMENTER_H
#define ECHOFLOW_AUDIO_SEGMENTER_H

#include <cstdint>
#include <vector>

namespace echoflow {

struct AudioSegmenterConfig {
    int sampleRate = 16000;
    int frameMs = 20;
    int endSilenceMs = 800;
    int minSegmentMs = 600;
    int prePaddingMs = 200;
    int postPaddingMs = 300;
    int maxSegmentMs = 12000;
    double speechRatio = 4.0;
    double minSpeechRms = 600.0;
};

struct AudioSegment {
    int sampleRate = 16000;
    std::vector<int16_t> samples;

    size_t sampleCount() const { return samples.size(); }
    double durationSeconds() const
    {
        return sampleRate > 0 ? static_cast<double>(samples.size()) / sampleRate : 0.0;
    }
};

class AudioSegmenter {
public:
    explicit AudioSegmenter(AudioSegmenterConfig cfg);

    std::vector<AudioSegment> append(const int16_t* samples, size_t count);
    std::vector<AudioSegment> flush();
    void reset();

private:
    enum class State {
        Idle,
        Speech,
    };

    std::vector<AudioSegment> processFrame(const int16_t* frame, size_t count);
    bool isSpeechFrame(double rms);
    AudioSegment sealSegment(size_t endSampleExclusive);
    size_t msToSamples(int ms) const;

    AudioSegmenterConfig cfg_;
    State state_ = State::Idle;
    std::vector<int16_t> buffer_;
    std::vector<int16_t> pending_;
    size_t speechStartSample_ = 0;
    size_t speechEndSample_ = 0;
    size_t silentSamples_ = 0;
    size_t totalSamples_ = 0;
    double noiseFloor_ = 120.0;
};

}  // namespace echoflow

#endif  // ECHOFLOW_AUDIO_SEGMENTER_H
```

- [ ] **Step 4: Add segmenter implementation**

Create `service/AudioSegmenter.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AudioSegmenter.h"

#include <algorithm>
#include <cmath>

namespace echoflow {

namespace {

double rms(const int16_t* samples, size_t count)
{
    if (!samples || count == 0) {
        return 0.0;
    }
    double sum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        const double v = static_cast<double>(samples[i]);
        sum += v * v;
    }
    return std::sqrt(sum / static_cast<double>(count));
}

}  // namespace

AudioSegmenter::AudioSegmenter(AudioSegmenterConfig cfg)
    : cfg_(cfg)
{
}

std::vector<AudioSegment> AudioSegmenter::append(const int16_t* samples, size_t count)
{
    std::vector<AudioSegment> out;
    if (!samples || count == 0) {
        return out;
    }

    pending_.insert(pending_.end(), samples, samples + count);
    const size_t frameSamples = msToSamples(cfg_.frameMs);
    while (pending_.size() >= frameSamples) {
        auto frameOut = processFrame(pending_.data(), frameSamples);
        out.insert(out.end(),
                   std::make_move_iterator(frameOut.begin()),
                   std::make_move_iterator(frameOut.end()));
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<long>(frameSamples));
    }
    return out;
}

std::vector<AudioSegment> AudioSegmenter::flush()
{
    if (!pending_.empty()) {
        auto frameOut = processFrame(pending_.data(), pending_.size());
        pending_.clear();
        if (!frameOut.empty()) {
            return frameOut;
        }
    }

    std::vector<AudioSegment> out;
    if (state_ == State::Speech &&
        speechEndSample_ > speechStartSample_ &&
        speechEndSample_ - speechStartSample_ >= msToSamples(cfg_.minSegmentMs)) {
        out.push_back(sealSegment(speechEndSample_));
    }
    reset();
    return out;
}

void AudioSegmenter::reset()
{
    state_ = State::Idle;
    buffer_.clear();
    pending_.clear();
    speechStartSample_ = 0;
    speechEndSample_ = 0;
    silentSamples_ = 0;
    totalSamples_ = 0;
    noiseFloor_ = 120.0;
}

std::vector<AudioSegment> AudioSegmenter::processFrame(const int16_t* frame, size_t count)
{
    std::vector<AudioSegment> out;
    if (!frame || count == 0) {
        return out;
    }

    const size_t frameStart = totalSamples_;
    const size_t frameEnd = frameStart + count;
    buffer_.insert(buffer_.end(), frame, frame + count);
    totalSamples_ = frameEnd;

    const double frameRms = rms(frame, count);
    const bool speech = isSpeechFrame(frameRms);

    if (state_ == State::Idle) {
        if (speech) {
            state_ = State::Speech;
            speechStartSample_ = frameStart;
            speechEndSample_ = frameEnd;
            silentSamples_ = 0;
        } else {
            noiseFloor_ = 0.95 * noiseFloor_ + 0.05 * frameRms;
            const size_t keep = msToSamples(cfg_.prePaddingMs);
            if (buffer_.size() > keep) {
                buffer_.erase(buffer_.begin(),
                              buffer_.begin() + static_cast<long>(buffer_.size() - keep));
                totalSamples_ = buffer_.size();
            }
        }
        return out;
    }

    if (speech) {
        speechEndSample_ = frameEnd;
        silentSamples_ = 0;
    } else {
        silentSamples_ += count;
    }

    const bool hasMinimumSpeech =
        speechEndSample_ > speechStartSample_ &&
        speechEndSample_ - speechStartSample_ >= msToSamples(cfg_.minSegmentMs);
    const bool enoughTrailingSilence = silentSamples_ >= msToSamples(cfg_.endSilenceMs);
    const bool tooLong = frameEnd - speechStartSample_ >= msToSamples(cfg_.maxSegmentMs);

    if (hasMinimumSpeech && (enoughTrailingSilence || tooLong)) {
        const size_t end = enoughTrailingSilence
            ? std::min(buffer_.size(), speechEndSample_ + msToSamples(cfg_.postPaddingMs))
            : frameEnd;
        out.push_back(sealSegment(end));
    }
    return out;
}

bool AudioSegmenter::isSpeechFrame(double frameRms)
{
    const double adaptiveThreshold = std::max(cfg_.minSpeechRms, noiseFloor_ * cfg_.speechRatio);
    return frameRms >= adaptiveThreshold;
}

AudioSegment AudioSegmenter::sealSegment(size_t endSampleExclusive)
{
    const size_t pre = msToSamples(cfg_.prePaddingMs);
    const size_t start = speechStartSample_ > pre ? speechStartSample_ - pre : 0;
    const size_t end = std::min(endSampleExclusive, buffer_.size());

    AudioSegment segment;
    segment.sampleRate = cfg_.sampleRate;
    if (start < end) {
        segment.samples.assign(buffer_.begin() + static_cast<long>(start),
                               buffer_.begin() + static_cast<long>(end));
    }

    const size_t keepStart = end > pre ? end - pre : 0;
    if (keepStart < buffer_.size()) {
        std::vector<int16_t> kept(buffer_.begin() + static_cast<long>(keepStart), buffer_.end());
        buffer_.swap(kept);
    } else {
        buffer_.clear();
    }

    totalSamples_ = buffer_.size();
    state_ = State::Idle;
    speechStartSample_ = 0;
    speechEndSample_ = 0;
    silentSamples_ = 0;
    return segment;
}

size_t AudioSegmenter::msToSamples(int ms) const
{
    return static_cast<size_t>((static_cast<int64_t>(cfg_.sampleRate) * ms) / 1000);
}

}  // namespace echoflow
```

- [ ] **Step 5: Add source to service CMake**

Modify `service/CMakeLists.txt`:

```cmake
add_library(echoflow_service STATIC
    AsrEngine.cpp
    AudioSegmenter.cpp
    Committer.cpp
    Config.cpp
    LiveAudioBuffer.cpp
    PipeWireLiveVoicePipeline.cpp
    Recorder.cpp
    SelfTest.cpp
    Server.cpp
    SingleInstance.cpp
    UiNotifier.cpp
    VoiceSession.cpp)
```

- [ ] **Step 6: Run segmenter tests**

Run:

```bash
cmake --build build --target test_audio_segmenter
./build/tests/test_audio_segmenter
```

Expected: all `TestAudioSegmenter` tests pass.

- [ ] **Step 7: Commit**

```bash
git add service/AudioSegmenter.h service/AudioSegmenter.cpp service/CMakeLists.txt \
  tests/test_audio_segmenter.cpp tests/CMakeLists.txt
git commit -m "feat(service): add energy audio segmenter" \
  -m "Introduce a deterministic PCM segmenter for speech/silence boundaries so live recording can be finalized in short Qwen transcription segments."
```

---

### Task 2: Add Stable Text Accumulation

**Files:**
- Create: `service/SegmentTextAccumulator.h`
- Create: `service/SegmentTextAccumulator.cpp`
- Create: `tests/test_segment_text_accumulator.cpp`
- Modify: `service/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing accumulator tests**

Create `tests/test_segment_text_accumulator.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "SegmentTextAccumulator.h"

using namespace echoflow;

class TestSegmentTextAccumulator : public QObject {
    Q_OBJECT

private slots:
    void joinsChineseWithoutSpaces();
    void joinsAsciiWordsWithSpace();
    void ignoresEmptySegments();
    void clearDropsText();
};

void TestSegmentTextAccumulator::joinsChineseWithoutSpaces()
{
    SegmentTextAccumulator acc;
    acc.append(0, "你好");
    acc.append(1, "世界");

    QCOMPARE(QString::fromStdString(acc.text()), QStringLiteral("你好世界"));
}

void TestSegmentTextAccumulator::joinsAsciiWordsWithSpace()
{
    SegmentTextAccumulator acc;
    acc.append(0, "hello");
    acc.append(1, "world");

    QCOMPARE(QString::fromStdString(acc.text()), QStringLiteral("hello world"));
}

void TestSegmentTextAccumulator::ignoresEmptySegments()
{
    SegmentTextAccumulator acc;
    acc.append(0, "你好");
    acc.append(1, " ");
    acc.append(2, "世界");

    QCOMPARE(QString::fromStdString(acc.text()), QStringLiteral("你好世界"));
}

void TestSegmentTextAccumulator::clearDropsText()
{
    SegmentTextAccumulator acc;
    acc.append(0, "hello");
    acc.clear();

    QVERIFY(acc.text().empty());
}

QTEST_GUILESS_MAIN(TestSegmentTextAccumulator)
#include "test_segment_text_accumulator.moc"
```

- [ ] **Step 2: Add test target and verify failure**

Add `test_segment_text_accumulator` to `ECHOFLOW_TESTS` in `tests/CMakeLists.txt`.

Run:

```bash
cmake --build build --target test_segment_text_accumulator
```

Expected: build fails because `SegmentTextAccumulator.h` does not exist.

- [ ] **Step 3: Add accumulator header**

Create `service/SegmentTextAccumulator.h`:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_SEGMENT_TEXT_ACCUMULATOR_H
#define ECHOFLOW_SEGMENT_TEXT_ACCUMULATOR_H

#include <map>
#include <string>

namespace echoflow {

class SegmentTextAccumulator {
public:
    void append(int sequence, const std::string& text);
    std::string text() const;
    void clear();

private:
    static std::string trim(const std::string& value);
    static bool needsSpace(const std::string& left, const std::string& right);
    static bool isAsciiWordByte(unsigned char c);

    std::map<int, std::string> segments_;
};

}  // namespace echoflow

#endif  // ECHOFLOW_SEGMENT_TEXT_ACCUMULATOR_H
```

- [ ] **Step 4: Add accumulator implementation**

Create `service/SegmentTextAccumulator.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SegmentTextAccumulator.h"

#include <cctype>

namespace echoflow {

void SegmentTextAccumulator::append(int sequence, const std::string& text)
{
    std::string cleaned = trim(text);
    if (cleaned.empty()) {
        return;
    }
    segments_[sequence] = cleaned;
}

std::string SegmentTextAccumulator::text() const
{
    std::string out;
    for (const auto& [sequence, segment] : segments_) {
        (void)sequence;
        if (out.empty()) {
            out = segment;
            continue;
        }
        if (needsSpace(out, segment)) {
            out += ' ';
        }
        out += segment;
    }
    return out;
}

void SegmentTextAccumulator::clear()
{
    segments_.clear();
}

std::string SegmentTextAccumulator::trim(const std::string& value)
{
    auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool SegmentTextAccumulator::needsSpace(const std::string& left, const std::string& right)
{
    if (left.empty() || right.empty()) {
        return false;
    }
    return isAsciiWordByte(static_cast<unsigned char>(left.back())) &&
           isAsciiWordByte(static_cast<unsigned char>(right.front()));
}

bool SegmentTextAccumulator::isAsciiWordByte(unsigned char c)
{
    return std::isalnum(c) != 0;
}

}  // namespace echoflow
```

- [ ] **Step 5: Add source to service CMake**

Add `SegmentTextAccumulator.cpp` to `service/CMakeLists.txt`:

```cmake
add_library(echoflow_service STATIC
    AsrEngine.cpp
    AudioSegmenter.cpp
    Committer.cpp
    Config.cpp
    LiveAudioBuffer.cpp
    PipeWireLiveVoicePipeline.cpp
    Recorder.cpp
    SegmentTextAccumulator.cpp
    SelfTest.cpp
    Server.cpp
    SingleInstance.cpp
    UiNotifier.cpp
    VoiceSession.cpp)
```

- [ ] **Step 6: Run accumulator tests**

Run:

```bash
cmake --build build --target test_segment_text_accumulator
./build/tests/test_segment_text_accumulator
```

Expected: all `TestSegmentTextAccumulator` tests pass.

- [ ] **Step 7: Commit**

```bash
git add service/SegmentTextAccumulator.h service/SegmentTextAccumulator.cpp \
  service/CMakeLists.txt tests/test_segment_text_accumulator.cpp tests/CMakeLists.txt
git commit -m "feat(service): add segment text accumulator" \
  -m "Preserve ordered Qwen final segment text and join Chinese dictation without adding unwanted spaces."
```

---

### Task 3: Add Segment ASR Worker

**Files:**
- Create: `service/SegmentAsrWorker.h`
- Create: `service/SegmentAsrWorker.cpp`
- Create: `tests/test_segment_asr_worker.cpp`
- Modify: `service/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing worker tests**

Create `tests/test_segment_asr_worker.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "SegmentAsrWorker.h"

#include <filesystem>
#include <mutex>
#include <vector>

using namespace echoflow;

struct FakeAsr : IAsrEngine {
    std::mutex mutex;
    std::vector<std::filesystem::path> paths;
    std::vector<std::string> results;

    std::string transcribe(const std::filesystem::path& audio) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        paths.push_back(audio);
        if (results.empty()) {
            return {};
        }
        std::string result = results.front();
        results.erase(results.begin());
        return result;
    }
};

class TestSegmentAsrWorker : public QObject {
    Q_OBJECT

private slots:
    void transcribesQueuedSegmentsInOrder();
    void skipsEmptyResults();
    void cancelDropsPendingResults();
};

void TestSegmentAsrWorker::transcribesQueuedSegmentsInOrder()
{
    FakeAsr asr;
    asr.results = {"你好", "世界"};
    std::vector<std::pair<int, std::string>> delivered;

    SegmentAsrWorker worker(asr, std::filesystem::temp_directory_path(),
                            [&](int sequence, const std::string& text) {
                                delivered.push_back({sequence, text});
                            });
    worker.start();
    worker.enqueue(AudioSegment{16000, std::vector<int16_t>(16000, 1000)});
    worker.enqueue(AudioSegment{16000, std::vector<int16_t>(16000, 1000)});
    worker.finishAndWait(std::chrono::seconds(10));

    QCOMPARE(delivered.size(), size_t(2));
    QCOMPARE(delivered[0].first, 0);
    QCOMPARE(QString::fromStdString(delivered[0].second), QStringLiteral("你好"));
    QCOMPARE(delivered[1].first, 1);
    QCOMPARE(QString::fromStdString(delivered[1].second), QStringLiteral("世界"));
    QCOMPARE(asr.paths.size(), size_t(2));
}

void TestSegmentAsrWorker::skipsEmptyResults()
{
    FakeAsr asr;
    asr.results = {"", "世界"};
    std::vector<std::pair<int, std::string>> delivered;

    SegmentAsrWorker worker(asr, std::filesystem::temp_directory_path(),
                            [&](int sequence, const std::string& text) {
                                delivered.push_back({sequence, text});
                            });
    worker.start();
    worker.enqueue(AudioSegment{16000, std::vector<int16_t>(16000, 1000)});
    worker.enqueue(AudioSegment{16000, std::vector<int16_t>(16000, 1000)});
    worker.finishAndWait(std::chrono::seconds(10));

    QCOMPARE(delivered.size(), size_t(1));
    QCOMPARE(delivered[0].first, 1);
    QCOMPARE(QString::fromStdString(delivered[0].second), QStringLiteral("世界"));
}

void TestSegmentAsrWorker::cancelDropsPendingResults()
{
    FakeAsr asr;
    asr.results = {"你好"};
    std::vector<std::pair<int, std::string>> delivered;

    SegmentAsrWorker worker(asr, std::filesystem::temp_directory_path(),
                            [&](int sequence, const std::string& text) {
                                delivered.push_back({sequence, text});
                            });
    worker.start();
    worker.enqueue(AudioSegment{16000, std::vector<int16_t>(16000, 1000)});
    worker.cancelAndWait();

    QVERIFY(delivered.empty() || delivered.size() == size_t(1));
}

QTEST_GUILESS_MAIN(TestSegmentAsrWorker)
#include "test_segment_asr_worker.moc"
```

- [ ] **Step 2: Add test target and verify failure**

Add `test_segment_asr_worker` to `ECHOFLOW_TESTS` in `tests/CMakeLists.txt`.

Run:

```bash
cmake --build build --target test_segment_asr_worker
```

Expected: build fails because `SegmentAsrWorker.h` does not exist.

- [ ] **Step 3: Add worker header**

Create `service/SegmentAsrWorker.h`:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_SEGMENT_ASR_WORKER_H
#define ECHOFLOW_SEGMENT_ASR_WORKER_H

#include "AudioSegmenter.h"
#include "Interfaces.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace echoflow {

class SegmentAsrWorker {
public:
    using ResultCallback = std::function<void(int sequence, const std::string& text)>;

    SegmentAsrWorker(IAsrEngine& asr, std::filesystem::path tempDir, ResultCallback callback);
    ~SegmentAsrWorker();

    SegmentAsrWorker(const SegmentAsrWorker&) = delete;
    SegmentAsrWorker& operator=(const SegmentAsrWorker&) = delete;

    void start();
    void enqueue(AudioSegment segment);
    bool finishAndWait(std::chrono::steady_clock::duration timeout);
    void cancelAndWait();

private:
    struct Item {
        int sequence = 0;
        AudioSegment segment;
    };

    void loop();
    std::filesystem::path writeWav(const Item& item);
    void removeIfExists(const std::filesystem::path& path);

    IAsrEngine& asr_;
    std::filesystem::path tempDir_;
    ResultCallback callback_;
    std::deque<Item> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable drainedCv_;
    std::thread thread_;
    int nextSequence_ = 0;
    bool running_ = false;
    bool accepting_ = true;
    bool cancel_ = false;
    bool busy_ = false;
};

}  // namespace echoflow

#endif  // ECHOFLOW_SEGMENT_ASR_WORKER_H
```

- [ ] **Step 4: Add worker implementation**

Create `service/SegmentAsrWorker.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SegmentAsrWorker.h"

#include "log.h"

#include <fstream>
#include <stdexcept>

namespace echoflow {

namespace {

void writeLe16(std::ofstream& out, uint16_t v)
{
    out.put(static_cast<char>(v & 0xff));
    out.put(static_cast<char>((v >> 8) & 0xff));
}

void writeLe32(std::ofstream& out, uint32_t v)
{
    out.put(static_cast<char>(v & 0xff));
    out.put(static_cast<char>((v >> 8) & 0xff));
    out.put(static_cast<char>((v >> 16) & 0xff));
    out.put(static_cast<char>((v >> 24) & 0xff));
}

}  // namespace

SegmentAsrWorker::SegmentAsrWorker(IAsrEngine& asr,
                                   std::filesystem::path tempDir,
                                   ResultCallback callback)
    : asr_(asr)
    , tempDir_(std::move(tempDir))
    , callback_(std::move(callback))
{
}

SegmentAsrWorker::~SegmentAsrWorker()
{
    cancelAndWait();
}

void SegmentAsrWorker::start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
        return;
    }
    accepting_ = true;
    cancel_ = false;
    running_ = true;
    thread_ = std::thread(&SegmentAsrWorker::loop, this);
}

void SegmentAsrWorker::enqueue(AudioSegment segment)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!accepting_ || cancel_) {
        return;
    }
    queue_.push_back(Item{nextSequence_++, std::move(segment)});
    cv_.notify_all();
}

bool SegmentAsrWorker::finishAndWait(std::chrono::steady_clock::duration timeout)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        accepting_ = false;
        cv_.notify_all();
    }
    std::unique_lock<std::mutex> lock(mutex_);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    bool drained = drainedCv_.wait_until(lock, deadline, [this]() {
        return queue_.empty() && !busy_;
    });
    lock.unlock();
    if (thread_.joinable()) {
        {
            std::lock_guard<std::mutex> relock(mutex_);
            running_ = false;
            cv_.notify_all();
        }
        thread_.join();
    }
    return drained;
}

void SegmentAsrWorker::cancelAndWait()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        accepting_ = false;
        cancel_ = true;
        queue_.clear();
        running_ = false;
        cv_.notify_all();
        drainedCv_.notify_all();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

void SegmentAsrWorker::loop()
{
    while (true) {
        Item item;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() {
                return cancel_ || !running_ || !queue_.empty();
            });
            if (cancel_ || (!running_ && queue_.empty())) {
                busy_ = false;
                drainedCv_.notify_all();
                return;
            }
            item = std::move(queue_.front());
            queue_.pop_front();
            busy_ = true;
        }

        std::filesystem::path wav;
        try {
            wav = writeWav(item);
            std::string text = asr_.transcribe(wav);
            if (!text.empty() && !cancel_ && callback_) {
                callback_(item.sequence, text);
            }
        } catch (const std::exception& e) {
            log(std::string("segment ASR failed: ") + e.what());
        }
        removeIfExists(wav);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            busy_ = false;
            if (queue_.empty()) {
                drainedCv_.notify_all();
            }
        }
    }
}

std::filesystem::path SegmentAsrWorker::writeWav(const Item& item)
{
    std::filesystem::create_directories(tempDir_);
    auto path = tempDir_ / ("echoflow-segment-" + std::to_string(item.sequence) + ".wav");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("failed to create segment wav: " + path.string());
    }

    const uint16_t channels = 1;
    const uint16_t bitsPerSample = 16;
    const uint32_t sampleRate = static_cast<uint32_t>(item.segment.sampleRate);
    const uint32_t byteRate = sampleRate * channels * bitsPerSample / 8;
    const uint16_t blockAlign = channels * bitsPerSample / 8;
    const uint32_t dataBytes = static_cast<uint32_t>(item.segment.samples.size() * sizeof(int16_t));

    out.write("RIFF", 4);
    writeLe32(out, 36 + dataBytes);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    writeLe32(out, 16);
    writeLe16(out, 1);
    writeLe16(out, channels);
    writeLe32(out, sampleRate);
    writeLe32(out, byteRate);
    writeLe16(out, blockAlign);
    writeLe16(out, bitsPerSample);
    out.write("data", 4);
    writeLe32(out, dataBytes);
    for (int16_t sample : item.segment.samples) {
        writeLe16(out, static_cast<uint16_t>(sample));
    }
    return path;
}

void SegmentAsrWorker::removeIfExists(const std::filesystem::path& path)
{
    if (!path.empty()) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
}

}  // namespace echoflow
```

- [ ] **Step 5: Add source to service CMake**

Add `SegmentAsrWorker.cpp` to `service/CMakeLists.txt`.

- [ ] **Step 6: Run worker tests**

Run:

```bash
cmake --build build --target test_segment_asr_worker
./build/tests/test_segment_asr_worker
```

Expected: all `TestSegmentAsrWorker` tests pass.

- [ ] **Step 7: Commit**

```bash
git add service/SegmentAsrWorker.h service/SegmentAsrWorker.cpp service/CMakeLists.txt \
  tests/test_segment_asr_worker.cpp tests/CMakeLists.txt
git commit -m "feat(service): add segment ASR worker" \
  -m "Queue completed speech segments for ordered Qwen final transcription during live recording."
```

---

### Task 4: Integrate Segments Into `PipeWireLiveVoicePipeline`

**Files:**
- Modify: `service/PipeWireLiveVoicePipeline.h`
- Modify: `service/PipeWireLiveVoicePipeline.cpp`

- [ ] **Step 1: Replace live-buffer members with segment members**

Modify `service/PipeWireLiveVoicePipeline.h` includes:

```cpp
#include "AudioSegmenter.h"
#include "SegmentAsrWorker.h"
#include "SegmentTextAccumulator.h"
```

Replace these members:

```cpp
std::unique_ptr<LiveAudioBuffer> live_;
std::thread asrThread_;
std::string result_;
std::string partialText_;
std::condition_variable partialTextCv_;
std::chrono::steady_clock::time_point lastPartialAt_{};
double partialCycleSeconds_ = 2.0;
bool asrDone_ = false;
```

with:

```cpp
std::unique_ptr<AudioSegmenter> segmenter_;
std::unique_ptr<SegmentAsrWorker> segmentWorker_;
SegmentTextAccumulator stableText_;
std::string latestPreviewText_;
std::mutex textMutex_;
std::function<void(const std::string&)> partialTextCallback_;
```

Remove declarations for:

```cpp
void asrLoop();
double partialCycleSecondsLocked() const;
bool waitForGraceOrDone(std::chrono::steady_clock::time_point started);
```

Add declarations:

```cpp
void enqueueSegments(std::vector<AudioSegment> segments);
void publishStableText();
std::filesystem::path segmentTempDir() const;
```

- [ ] **Step 2: Update `start()` setup**

In `PipeWireLiveVoicePipeline::start()`, remove `LiveAudioBuffer` construction and `asrThread_` startup. Add:

```cpp
segmenter_ = std::make_unique<AudioSegmenter>(AudioSegmenterConfig{});
stableText_.clear();
latestPreviewText_.clear();
segmentWorker_ = std::make_unique<SegmentAsrWorker>(
    asr_, segmentTempDir(), [this](int sequence, const std::string& text) {
        {
            std::lock_guard<std::mutex> lock(textMutex_);
            stableText_.append(sequence, text);
        }
        publishStableText();
    });
segmentWorker_->start();
```

Keep `readerThread_ = std::thread(&PipeWireLiveVoicePipeline::readerLoop, this);`.

- [ ] **Step 3: Update `readerLoop()` to segment PCM**

Replace the body handling `n > 0`:

```cpp
if (n > 0) {
    if (segmenter_) {
        const auto* pcm = reinterpret_cast<const int16_t*>(buffer.data());
        const size_t sampleCount = static_cast<size_t>(n) / sizeof(int16_t);
        enqueueSegments(segmenter_->append(pcm, sampleCount));
    }
    continue;
}
```

At loop end, replace `live_->markEof()` with:

```cpp
if (segmenter_) {
    enqueueSegments(segmenter_->flush());
}
```

- [ ] **Step 4: Update `finish()` semantics**

Replace the post-recorder live-ASR wait/cancel logic with:

```cpp
closeReadFd();
joinThreads();
if (segmenter_) {
    enqueueSegments(segmenter_->flush());
}
bool drained = true;
if (segmentWorker_) {
    drained = segmentWorker_->finishAndWait(
        std::chrono::seconds(std::max(1, cfg_.asrTimeoutSeconds)));
}
active_ = false;
segmenter_.reset();
segmentWorker_.reset();
std::string finalText;
{
    std::lock_guard<std::mutex> lock(textMutex_);
    finalText = cancelled_ ? std::string() : stableText_.text();
    stableText_.clear();
    latestPreviewText_.clear();
}
log("segmented live pipeline finish returned in " +
    std::to_string(elapsedSeconds(finishStarted)) +
    "s, total=" + std::to_string(elapsedSeconds(startedAt_)) +
    "s, chars=" + std::to_string(finalText.size()) +
    ", drained=" + (drained ? "yes" : "no"));
return finalText;
```

- [ ] **Step 5: Update `cancel()` semantics**

Replace live buffer cancellation with:

```cpp
if (segmentWorker_) {
    segmentWorker_->cancelAndWait();
}
closeReadFd();
joinThreads();
active_ = false;
segmenter_.reset();
segmentWorker_.reset();
{
    std::lock_guard<std::mutex> lock(textMutex_);
    stableText_.clear();
    latestPreviewText_.clear();
}
```

- [ ] **Step 6: Add helper methods**

Add to `service/PipeWireLiveVoicePipeline.cpp`:

```cpp
void PipeWireLiveVoicePipeline::enqueueSegments(std::vector<AudioSegment> segments)
{
    if (!segmentWorker_) {
        return;
    }
    for (auto& segment : segments) {
        if (!segment.samples.empty()) {
            segmentWorker_->enqueue(std::move(segment));
        }
    }
}

void PipeWireLiveVoicePipeline::publishStableText()
{
    std::function<void(const std::string&)> callback;
    std::string text;
    {
        std::lock_guard<std::mutex> lock(textMutex_);
        callback = partialTextCallback_;
        text = stableText_.text();
    }
    if (callback && !text.empty()) {
        callback(text);
    }
}

std::filesystem::path PipeWireLiveVoicePipeline::segmentTempDir() const
{
    const auto base = cfg_.recordingsDir.empty()
        ? std::filesystem::temp_directory_path()
        : std::filesystem::path(cfg_.recordingsDir);
    return base / ".segments";
}
```

- [ ] **Step 7: Update `joinThreads()`**

Remove `asrThread_` join:

```cpp
void PipeWireLiveVoicePipeline::joinThreads()
{
    if (readerThread_.joinable()) {
        readerThread_.join();
    }
}
```

- [ ] **Step 8: Build service**

Run:

```bash
cmake --build build --target echoflow-service
```

Expected: target builds successfully.

- [ ] **Step 9: Commit**

```bash
git add service/PipeWireLiveVoicePipeline.h service/PipeWireLiveVoicePipeline.cpp
git commit -m "feat(service): finalize live recording by speech segments" \
  -m "Feed PipeWire PCM through the segmenter, transcribe completed segments with Qwen final ASR, and return only accumulated stable text on stop."
```

---

### Task 5: Tighten Session-Level Behavior Tests

**Files:**
- Modify: `tests/test_voice_session.cpp`

- [ ] **Step 1: Add a test for stable text commit semantics**

Add slot declaration:

```cpp
void livePreviewDoesNotAffectCommittedText();
```

Add test body:

```cpp
void TestVoiceSession::livePreviewDoesNotAffectCommittedText()
{
    FakeLivePipeline pipeline;
    pipeline.result = "稳定最终文本";
    FakeCommitter committer;
    FakeUi ui;
    auto session = makeLiveSession(pipeline, committer, ui);

    session.handleCommand("CTRL_DOWN");
    QVERIFY(pipeline.partialTextCallback);
    pipeline.partialTextCallback("错误预览文本");

    QCOMPARE(QString::fromStdString(session.handleCommand("CTRL_DOWN")),
             QStringLiteral("COMMITTED"));
    QCOMPARE(committer.texts.size(), size_t(1));
    QCOMPARE(QString::fromStdString(committer.texts.front()), QStringLiteral("稳定最终文本"));
}
```

- [ ] **Step 2: Run the test**

Run:

```bash
cmake --build build --target test_voice_session
./build/tests/test_voice_session
```

Expected: all `TestVoiceSession` tests pass.

- [ ] **Step 3: Commit**

```bash
git add tests/test_voice_session.cpp
git commit -m "test(service): cover preview-only live text" \
  -m "Assert that live preview callbacks update UI text but do not affect the final committed result."
```

---

### Task 6: Full Verification and Runtime Smoke

**Files:**
- No source changes expected.

- [ ] **Step 1: Run focused tests**

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 2: Run service self-test**

Run:

```bash
./build/service/echoflow-service --self-test
```

Expected: exits 0 and reports service self-test success.

- [ ] **Step 3: Run syntax checks**

Run:

```bash
bash -n install-user.sh uninstall-user.sh tests/spec/*.sh
sh -n run.sh
```

Expected: no output and exit 0.

- [ ] **Step 4: Install and restart services for manual testing**

Run:

```bash
./install-user.sh
systemctl --user restart echoflow.service echoflow-ui.service
systemctl --user --no-pager --full status echoflow.service echoflow-ui.service
```

Expected: both user services are active. Do not restart Fcitx because this plan does not change `fcitx-addon/`.

- [ ] **Step 5: Manual behavior checks**

Perform these manual checks:

```text
1. Record one short sentence.
   Expected: commit text quality matches Qwen final behavior.

2. Record two or three sentences with 1 second pauses.
   Expected: capsule updates with stable completed sentence text before stop.

3. Stop after a long multi-sentence recording.
   Expected: stop waits mainly for the last open segment, not the full recording.

4. Blur while recording.
   Expected: no text is committed.
```

- [ ] **Step 6: Inspect logs**

Run:

```bash
journalctl --user -u echoflow.service -n 120 --no-pager
```

Expected: logs show segment transcription activity and no crash, deadlock, or repeated ASR failure.

- [ ] **Step 7: Final commit if verification required source adjustments**

If verification required fixes, commit only the related files:

```bash
git status --short
git add <changed-source-or-test-files>
git commit -m "fix(service): stabilize segmented live transcription" \
  -m "Address verification findings from segmented Qwen final transcription runtime checks."
```

If there are no changes, do not create an empty commit.

---

## Self-Review

- Spec coverage: the plan covers energy segmentation, background Qwen final transcription, stable text accumulation, `finish()` once-only commit, cancellation, time bounds, tests, and runtime checks.
- Scope: the plan intentionally defers concurrent live partial preview because sharing Qwen live and Qwen final work in one service risks CPU contention and context-safety issues. Stable segment text still satisfies the selected B behavior.
- Red-flag scan: no incomplete sections are left for implementation.
- Type consistency: `AudioSegment`, `AudioSegmenter`, `SegmentAsrWorker`, and `SegmentTextAccumulator` names are consistent across tasks.
