# Live Silero VAD Trial Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an explicitly selectable, persistent-model Silero segmenter to EchoFlow's live recording path and enable it on the current machine without changing the compiled energy-VAD default.

**Architecture:** Introduce an `ILiveSegmenter` boundary shared by the existing energy segmenter and a new rolling-buffer `SileroLiveSegmenter`. Keep CrispASR model ownership inside `SileroVadBackend`, select implementations in a small factory, and leave `LiveSegmentCoordinator` responsible only for queueing ordered `AudioSegment` objects into ASR.

**Tech Stack:** C++17, CMake, Qt6 QTest, vendored CrispASR C API, systemd user services, DTK-style INI configuration.

---

## File Map

- Create `service/LiveSegmenter.h`: common `AudioSegment` value type and the live segmenter interface.
- Modify `service/AudioSegmenter.h/.cpp`: implement `ILiveSegmenter` without changing energy behavior.
- Create `service/SileroLiveSegmenter.h/.cpp`: rolling endpoint state machine using injected `IVadBackend`.
- Modify `service/SileroVadBackend.h/.cpp`: load one Silero context and reuse it across detection calls.
- Create `service/LiveSegmenterFactory.h/.cpp`: map `Config` to energy or Silero implementations.
- Modify `service/LiveSegmentCoordinator.h/.cpp`: own an abstract segmenter and retain an energy convenience constructor.
- Modify `service/CrispLiveVoicePipeline.cpp`: construct the configured backend through the factory.
- Modify `service/Config.h/.cpp` and `service/main.cpp`: parse, validate, and print Silero trial settings.
- Modify `service/CMakeLists.txt`: compile the new production sources.
- Create `tests/test_silero_live_segmenter.cpp`: deterministic endpoint tests with a fake VAD.
- Create `tests/test_live_segmenter_factory.cpp`: selection and fail-closed tests without model weights.
- Modify `tests/test_audio_segmenter.cpp`, `tests/test_segment_asr_worker.cpp`, `tests/test_vad_backend.cpp`, `tests/test_config.cpp`, and `tests/CMakeLists.txt`: interface, lifecycle, and configuration coverage.
- Modify `tests/benchmarks/vad_replay_benchmark.cpp`: reuse one configured Silero backend for all manifest clips.
- Modify only the local runtime copies of `~/.config/echoflow/echoflow.conf` and `~/.config/echoflow/vad/silero-v6.2.0-ggml.bin` during activation; do not commit them.

### Task 1: Parse and validate the Silero trial configuration

**Files:**
- Modify: `service/Config.h`
- Modify: `service/Config.cpp`
- Modify: `service/main.cpp`
- Test: `tests/test_config.cpp`

- [ ] **Step 1: Add failing assertions for defaults, parsing, and validation**

Extend `TestConfig` with `loadDtkConfRejectsInvalidVadSettings()` and update the
existing VAD assertions:

```cpp
// In defaultConfigHasExpectedFields().
QCOMPARE(c.vadThreshold, 0.10f);
QCOMPARE(c.vadMinSpeechMs, 100);
QCOMPARE(c.vadMinSilenceMs, 300);

// In loadDtkConfReadsVadSettings(), append these sections to the fixture.
"[advanced.vad.threshold]\nvalue=0.25\n"
"[advanced.vad.min_speech_ms]\nvalue=120\n"
"[advanced.vad.min_silence_ms]\nvalue=450\n"

QCOMPARE(c.vadThreshold, 0.25f);
QCOMPARE(c.vadMinSpeechMs, 120);
QCOMPARE(c.vadMinSilenceMs, 450);

void TestConfig::loadDtkConfRejectsInvalidVadSettings()
{
    const std::vector<QByteArray> invalid = {
        "[advanced.vad.backend]\nvalue=unknown\n",
        "[advanced.vad.threshold]\nvalue=0\n",
        "[advanced.vad.threshold]\nvalue=1.1\n",
        "[advanced.vad.min_speech_ms]\nvalue=0\n",
        "[advanced.vad.min_silence_ms]\nvalue=-1\n",
    };
    for (const QByteArray& contents : invalid) {
        QTemporaryFile file;
        QVERIFY(file.open());
        file.write(contents);
        file.close();
        QVERIFY_EXCEPTION_THROWN(loadDtkConf(file.fileName().toStdString()),
                                 std::invalid_argument);
    }
}
```

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target test_config -j"$(nproc)"
ctest --test-dir build -R '^test_config$' --output-on-failure
```

Expected: compilation fails because `Config` has no new fields, proving the
test exercises the missing behavior.

- [ ] **Step 3: Add fields, parser branches, and one validation function**

Add to `Config`:

```cpp
float vadThreshold = 0.10f;
int vadMinSpeechMs = 100;
int vadMinSilenceMs = 300;
```

Parse the three DTK sections in `loadDtkConf()`:

```cpp
} else if (section == "advanced.vad.threshold") {
    cfg.vadThreshold = std::stof(val);
} else if (section == "advanced.vad.min_speech_ms") {
    cfg.vadMinSpeechMs = std::stoi(val);
} else if (section == "advanced.vad.min_silence_ms") {
    cfg.vadMinSilenceMs = std::stoi(val);
}
```

Before path expansion, validate without silent clamping:

```cpp
if (cfg.vadBackend != "energy" && cfg.vadBackend != "silero") {
    throw std::invalid_argument("unsupported VAD backend: " + cfg.vadBackend);
}
if (!(cfg.vadThreshold > 0.0f && cfg.vadThreshold <= 1.0f)) {
    throw std::invalid_argument("VAD threshold must be in (0, 1]");
}
if (cfg.vadMinSpeechMs <= 0 || cfg.vadMinSilenceMs <= 0) {
    throw std::invalid_argument("VAD durations must be positive");
}
```

Add `<stdexcept>` to production and test includes. Extend
`printDefaultConfig()` with `vad_threshold`, `vad_min_speech_ms`, and
`vad_min_silence_ms` fields and matching arguments.

- [ ] **Step 4: Run the focused test and verify GREEN**

Run:

```bash
cmake --build build --target test_config echoflow-service -j"$(nproc)"
ctest --test-dir build -R '^test_config$' --output-on-failure
./build/service/echoflow-service --print-default-config | rg 'vad_(threshold|min_speech_ms|min_silence_ms)'
```

Expected: `test_config` passes and the service prints `0.10`, `100`, and
`300`.

- [ ] **Step 5: Commit the configuration change**

```bash
git add service/Config.h service/Config.cpp service/main.cpp tests/test_config.cpp
git commit -m "feat(service): configure Silero VAD endpointing" \
  -m "Expose validated threshold and duration settings needed for a local Silero trial." \
  -m "Leave energy VAD as the compiled default."
```

### Task 2: Introduce the live segmenter boundary

**Files:**
- Create: `service/LiveSegmenter.h`
- Modify: `service/AudioSegmenter.h`
- Modify: `service/AudioSegmenter.cpp`
- Modify: `service/LiveSegmentCoordinator.h`
- Modify: `service/LiveSegmentCoordinator.cpp`
- Test: `tests/test_audio_segmenter.cpp`
- Test: `tests/test_segment_asr_worker.cpp`

- [ ] **Step 1: Write a failing injected-segmenter coordinator test**

Add this fake and test to `tests/test_segment_asr_worker.cpp`:

```cpp
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
```

Register the slot and include `<memory>` and `LiveSegmenter.h`.

- [ ] **Step 2: Run the coordinator test and verify RED**

Run:

```bash
cmake --build build --target test_segment_asr_worker -j"$(nproc)"
```

Expected: compilation fails because `ILiveSegmenter` and the injected
constructor do not exist.

- [ ] **Step 3: Add the common interface and adapt energy VAD**

Create `service/LiveSegmenter.h` with SPDX headers and these exact public
types:

```cpp
struct AudioSegment {
    int sampleRate = 16000;
    uint64_t beginSample = 0;
    uint64_t endSample = 0;
    std::vector<int16_t> samples;

    size_t sampleCount() const { return samples.size(); }
    double durationSeconds() const {
        return sampleRate > 0
            ? static_cast<double>(samples.size()) / static_cast<double>(sampleRate)
            : 0.0;
    }
};

class ILiveSegmenter {
public:
    virtual ~ILiveSegmenter() = default;
    virtual std::vector<AudioSegment> append(const int16_t* samples, size_t count) = 0;
    virtual std::optional<AudioSegment> flush() = 0;
    virtual void reset() = 0;
    virtual std::string name() const = 0;
    virtual std::string diagnosticSummary() const = 0;
};
```

Move `AudioSegment` out of `AudioSegmenter.h`, make `AudioSegmenter final :
public ILiveSegmenter`, mark lifecycle methods `override`, and add:

```cpp
std::string name() const override { return "energy"; }
std::string diagnosticSummary() const override;
```

Remove the old out-of-line `AudioSegment` methods. Implement
`diagnosticSummary()` using the existing fields and `std::to_string()` so it
returns:

```text
frames=N, min_rms=N, max_rms=N, below40_ms=N, below80_ms=N, below120_ms=N, below200_ms=N
```

- [ ] **Step 4: Make the coordinator own `ILiveSegmenter`**

Add the injected constructor and preserve the existing constructor:

```cpp
LiveSegmentCoordinator(std::unique_ptr<ILiveSegmenter> segmenter,
                       SegmentAsrWorker::Transcribe transcribe,
                       SegmentAsrWorker::ResultCallback callback = {});
LiveSegmentCoordinator(AudioSegmenterConfig config,
                       SegmentAsrWorker::Transcribe transcribe,
                       SegmentAsrWorker::ResultCallback callback = {});
```

Store `std::unique_ptr<ILiveSegmenter> segmenter_`. Delegate the energy
constructor with `std::make_unique<AudioSegmenter>(config)`. Reject a null
injected pointer with `std::invalid_argument`. Replace member access with
`segmenter_->append/flush/reset`. At `start()` log:

```cpp
log("live VAD backend: " + segmenter_->name());
```

At `finish()` replace the energy-only log with:

```cpp
log("live VAD " + segmenter_->name() + ": " + segmenter_->diagnosticSummary());
```

- [ ] **Step 5: Pin energy interface behavior and verify GREEN**

Add to `defaultConfigPinsSegmenterParameters()`:

```cpp
AudioSegmenter segmenter(config);
QCOMPARE(QString::fromStdString(segmenter.name()), QStringLiteral("energy"));
QVERIFY(QString::fromStdString(segmenter.diagnosticSummary()).contains("frames=0"));
```

Run:

```bash
cmake --build build --target test_audio_segmenter test_segment_asr_worker -j"$(nproc)"
ctest --test-dir build -R '^(test_audio_segmenter|test_segment_asr_worker)$' --output-on-failure
```

Expected: both tests pass and all existing energy segmentation assertions are
unchanged.

- [ ] **Step 6: Commit the interface refactor**

```bash
git add service/LiveSegmenter.h service/AudioSegmenter.h service/AudioSegmenter.cpp \
  service/LiveSegmentCoordinator.h service/LiveSegmentCoordinator.cpp \
  tests/test_audio_segmenter.cpp tests/test_segment_asr_worker.cpp
git commit -m "refactor(service): abstract live audio segmentation" \
  -m "Let the live coordinator accept interchangeable segmenters." \
  -m "Preserve every existing energy-VAD behavior and metric."
```

### Task 3: Reuse one native Silero model context

**Files:**
- Modify: `service/SileroVadBackend.h`
- Modify: `service/SileroVadBackend.cpp`
- Test: `tests/test_vad_backend.cpp`
- Modify: `tests/benchmarks/vad_replay_benchmark.cpp`

- [ ] **Step 1: Change lifecycle tests to require fail-fast construction**

Replace the missing and corrupt model bodies with:

```cpp
QVERIFY_EXCEPTION_THROWN(
    SileroVadBackend("/definitely/missing/ggml-silero.bin", 0.1f, 100, 300),
    std::runtime_error);

QVERIFY_EXCEPTION_THROWN(
    SileroVadBackend(path, 0.1f, 100, 300),
    std::runtime_error);
```

Add a `rejectsInvalidParameters()` case:

```cpp
QVERIFY_EXCEPTION_THROWN(
    SileroVadBackend("/unused", 0.0f, 100, 300), std::invalid_argument);
QVERIFY_EXCEPTION_THROWN(
    SileroVadBackend("/unused", 0.1f, 0, 300), std::invalid_argument);
QVERIFY_EXCEPTION_THROWN(
    SileroVadBackend("/unused", 0.1f, 100, 0), std::invalid_argument);
```

- [ ] **Step 2: Run the backend test and verify RED**

Run:

```bash
cmake --build build --target test_vad_backend -j"$(nproc)"
ctest --test-dir build -R '^test_vad_backend$' --output-on-failure
```

Expected: compilation fails on the four-argument constructor.

- [ ] **Step 3: Implement persistent context ownership**

Forward-declare `struct whisper_vad_context;` and change the class to:

```cpp
class SileroVadBackend final : public IVadBackend {
public:
    explicit SileroVadBackend(std::filesystem::path modelPath,
                              float threshold = 0.5f,
                              int minSpeechMs = 100,
                              int minSilenceMs = 300);
    ~SileroVadBackend() override;
    SileroVadBackend(const SileroVadBackend&) = delete;
    SileroVadBackend& operator=(const SileroVadBackend&) = delete;

    std::vector<SpeechInterval> detect(const int16_t* pcm, size_t count) override;
    std::string name() const override;

private:
    std::filesystem::path modelPath_;
    float threshold_;
    int minSpeechMs_;
    int minSilenceMs_;
    whisper_vad_context* context_ = nullptr;
};
```

In the constructor, validate numeric arguments before checking the file, then
initialize a CPU VAD context once:

```cpp
whisper_vad_context_params params = whisper_vad_default_context_params();
params.n_threads = 1;
params.use_gpu = false;
params.gpu_device = 0;
context_ = whisper_vad_init_from_file_with_params(modelPath_.c_str(), params);
if (!context_) {
    throw std::runtime_error("failed to load Silero VAD model: " + modelPath_.string());
}
```

The destructor calls `whisper_vad_free(context_)`. `detect()` converts S16 to
F32, calls `whisper_vad_detect_speech()`, then
`whisper_vad_segments_from_probs()` using the configured threshold and
durations. Copy centisecond intervals to sample offsets, free the temporary
segments, and throw on inference failure. Empty PCM still returns an empty
vector without invoking the model.

- [ ] **Step 4: Reuse one backend in the replay benchmark**

Construct `std::unique_ptr<SileroVadBackend> silero` once before the manifest
loop when `backend == "silero"`, passing threshold, `100`, and
`endSilenceMs`. Replace the per-clip construction with `silero->detect(...)`.
This keeps the benchmark's output schema unchanged while matching live model
lifetime.

- [ ] **Step 5: Verify GREEN**

Run:

```bash
cmake --build build --target test_vad_backend vad_replay_benchmark -j"$(nproc)"
ctest --test-dir build -R '^test_vad_backend$' --output-on-failure
```

Expected: lifecycle tests pass without requiring a valid model fixture.

- [ ] **Step 6: Commit persistent Silero ownership**

```bash
git add service/SileroVadBackend.h service/SileroVadBackend.cpp \
  tests/test_vad_backend.cpp tests/benchmarks/vad_replay_benchmark.cpp
git commit -m "perf(service): reuse the Silero VAD model" \
  -m "Keep one native context alive across repeated detection passes." \
  -m "Avoid reloading the model for every live PCM block."
```

### Task 4: Implement rolling Silero endpointing

**Files:**
- Create: `service/SileroLiveSegmenter.h`
- Create: `service/SileroLiveSegmenter.cpp`
- Create: `tests/test_silero_live_segmenter.cpp`
- Modify: `service/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add deterministic failing live-endpoint tests**

Create this QTest binary. `MarkerVadBackend` returns contiguous ranges whose
sample value is non-zero, so endpoint tests do not need model weights:

```cpp
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

void TestSileroLiveSegmenter::unfinishedIntervalWaitsForEndpointSilence()
{
    auto segmenter = makeSegmenter();
    std::vector<int16_t> input = pcm(200, 0);
    appendPcm(input, pcm(800, 1200));
    appendPcm(input, pcm(200, 0));
    QCOMPARE(segmenter->append(input.data(), input.size()).size(), size_t(0));
    QVERIFY(segmenter->flush().has_value());
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
```

Declare all seven functions as private slots and finish with
`QTEST_GUILESS_MAIN(TestSileroLiveSegmenter)` plus the generated MOC include.

- [ ] **Step 2: Register the target and verify RED**

Add `test_silero_live_segmenter` to `ECHOFLOW_TESTS`, then run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target test_silero_live_segmenter -j"$(nproc)"
```

Expected: compilation fails because `SileroLiveSegmenter` does not exist.

- [ ] **Step 3: Add the live segmenter public API**

Create `SileroLiveSegmenterConfig` with these defaults:

```cpp
struct SileroLiveSegmenterConfig {
    int sampleRate = 16000;
    int analysisCadenceMs = 100;
    int endSilenceMs = 300;
    int prePaddingMs = 200;
    int postPaddingMs = 200;
    int maxSegmentMs = 8000;
    int forceOverlapMs = 500;
};
```

Create `SileroLiveSegmenter final : public ILiveSegmenter` with this API:

```cpp
SileroLiveSegmenter(SileroLiveSegmenterConfig config,
                    std::unique_ptr<IVadBackend> backend);
std::vector<AudioSegment> append(const int16_t*, size_t) override;
std::optional<AudioSegment> flush() override;
void reset() override;
std::string name() const override { return "silero"; }
std::string diagnosticSummary() const override;
```

Store the unfinalized PCM, its absolute start offset, the number of samples
since analysis, analysis count, and cumulative analysis milliseconds. Reject a
null backend, non-16-kHz input, non-positive cadence/silence/max duration, or
an overlap not smaller than maximum duration.

- [ ] **Step 4: Implement natural and forced boundaries**

On `append()`, add PCM to the rolling buffer and analyze whenever cadence is
reached. One analysis pass must:

```cpp
const auto intervals = backend_->detect(buffer_.data(), buffer_.size());
size_t consumed = 0;
for (const SpeechInterval& raw : intervals) {
    const size_t begin = std::min(raw.beginSample, buffer_.size());
    const size_t end = std::min(raw.endSample, buffer_.size());
    if (end <= begin || buffer_.size() - end < endSilenceSamples_) continue;
    const size_t paddedBegin = std::max(consumed,
        begin > prePaddingSamples_ ? begin - prePaddingSamples_ : size_t(0));
    const size_t paddedEnd = std::min(buffer_.size(), end + postPaddingSamples_);
    if (paddedEnd > paddedBegin) {
        emitted.push_back(makeSegment(paddedBegin, paddedEnd));
        consumed = paddedEnd;
    }
}
if (consumed > 0) erasePrefix(consumed);
```

If there is speech but no final interval and the rolling speech window reaches
`maxSegmentMs`, emit at most the configured maximum and erase only through
`end - forceOverlapSamples_`. If there is no speech, retain only
`prePaddingMs` of tail silence so the buffer remains bounded. If the first
open speech begins after leading silence, trim only the silence before its
pre-roll.

`makeSegment()` adds `bufferStartSample_` to relative offsets. `erasePrefix()`
updates both the buffer and absolute start. Re-run analysis after an emission
while enough buffered PCM remains so a large input block cannot hide a second
closed interval.

`flush()` runs one final detection pass. If speech remains, return one segment
from the first interval's bounded pre-roll through the last interval's bounded
post-roll, then reset. If no interval remains, reset and return `nullopt`.

- [ ] **Step 5: Verify GREEN and guard regressions**

Run:

```bash
cmake --build build --target test_silero_live_segmenter test_audio_segmenter -j"$(nproc)"
ctest --test-dir build -R '^(test_silero_live_segmenter|test_audio_segmenter)$' --output-on-failure
```

Expected: all deterministic Silero endpoint cases and all existing energy
cases pass.

- [ ] **Step 6: Commit live Silero endpointing**

```bash
git add service/SileroLiveSegmenter.h service/SileroLiveSegmenter.cpp \
  service/CMakeLists.txt tests/test_silero_live_segmenter.cpp tests/CMakeLists.txt
git commit -m "feat(service): segment live audio with Silero VAD" \
  -m "Finalize speech at neural VAD pauses and retain short utterances and padding." \
  -m "Keep the existing forced boundary for continuous speech."
```

### Task 5: Select Silero in the production live pipeline

**Files:**
- Create: `service/LiveSegmenterFactory.h`
- Create: `service/LiveSegmenterFactory.cpp`
- Modify: `service/CrispLiveVoicePipeline.cpp`
- Modify: `service/SelfTest.cpp`
- Modify: `service/CMakeLists.txt`
- Create: `tests/test_live_segmenter_factory.cpp`
- Modify: `tests/CMakeLists.txt`
- Test: `tests/test_selftest.cpp`

- [ ] **Step 1: Add failing factory and self-test cases**

Create `tests/test_live_segmenter_factory.cpp` with:

```cpp
void TestLiveSegmenterFactory::energyIsTheDefault()
{
    Config config;
    auto segmenter = makeLiveSegmenter(config);
    QCOMPARE(QString::fromStdString(segmenter->name()), QStringLiteral("energy"));
}

void TestLiveSegmenterFactory::unknownBackendFailsClosed()
{
    Config config;
    config.vadBackend = "unknown";
    QVERIFY_EXCEPTION_THROWN(makeLiveSegmenter(config), std::invalid_argument);
}

void TestLiveSegmenterFactory::missingSileroModelFailsClosed()
{
    Config config;
    config.vadBackend = "silero";
    config.vadModelPath = "/definitely/missing/silero.bin";
    QVERIFY_EXCEPTION_THROWN(makeLiveSegmenter(config), std::runtime_error);
}
```

Extend `tests/test_selftest.cpp` with a direct `Config` using an unknown backend
and assert a failed `VAD backend supported` check is present. Register the new
test target.

- [ ] **Step 2: Run tests and verify RED**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target test_live_segmenter_factory test_selftest -j"$(nproc)"
```

Expected: compilation fails because the factory does not exist.

- [ ] **Step 3: Implement the factory**

Expose:

```cpp
std::unique_ptr<ILiveSegmenter> makeLiveSegmenter(const Config& config);
```

Implement exact selection:

```cpp
if (config.vadBackend == "energy") {
    AudioSegmenterConfig energy;
    energy.sampleRate = config.pwRecord.rate;
    return std::make_unique<AudioSegmenter>(energy);
}
if (config.vadBackend == "silero") {
    SileroLiveSegmenterConfig live;
    live.sampleRate = config.pwRecord.rate;
    live.endSilenceMs = config.vadMinSilenceMs;
    auto backend = std::make_unique<SileroVadBackend>(
        config.vadModelPath, config.vadThreshold,
        config.vadMinSpeechMs, config.vadMinSilenceMs);
    return std::make_unique<SileroLiveSegmenter>(live, std::move(backend));
}
throw std::invalid_argument("unsupported VAD backend: " + config.vadBackend);
```

Add the two factory sources to `echoflow_service`.

- [ ] **Step 4: Wire the factory into live startup and self-test**

Replace the local `AudioSegmenterConfig` construction in
`CrispLiveVoicePipeline::start()` with the complete coordinator construction:

```cpp
auto segmenter = makeLiveSegmenter(cfg_);
coordinator_ = std::make_unique<LiveSegmentCoordinator>(
    std::move(segmenter),
    [this](const AudioSegment& segment) {
        auto f32 = toFloat32(segment.samples);
        return session_->transcribe(f32.data(), static_cast<int>(f32.size()));
    },
    [this](const std::vector<std::string>& results) {
        const std::string text = joinText(results);
        log("live partial emitted: segments=" + std::to_string(results.size())
            + ", chars=" + std::to_string(text.size()));
        emitText(text);
    });
```

Include `LiveSegmenterFactory.h` and remove the now-unused `AudioSegmenter`
include.

Add an unconditional self-test check:

```cpp
const bool backendSupported = cfg.vadBackend == "energy" || cfg.vadBackend == "silero";
checks.push_back({"VAD backend supported", backendSupported, cfg.vadBackend});
```

Retain the existing selected-Silero model availability check.

- [ ] **Step 5: Verify production selection GREEN**

Run:

```bash
cmake --build build --target test_live_segmenter_factory test_selftest echoflow-service -j"$(nproc)"
ctest --test-dir build -R '^(test_live_segmenter_factory|test_selftest)$' --output-on-failure
```

Expected: energy selection passes, unknown selection fails closed, and a
missing selected Silero model produces an actionable exception/check.

- [ ] **Step 6: Commit production integration**

```bash
git add service/LiveSegmenterFactory.h service/LiveSegmenterFactory.cpp \
  service/CrispLiveVoicePipeline.cpp service/SelfTest.cpp service/CMakeLists.txt \
  tests/test_live_segmenter_factory.cpp tests/test_selftest.cpp tests/CMakeLists.txt
git commit -m "feat(service): select VAD in the live pipeline" \
  -m "Honor the configured energy or Silero backend at recording startup." \
  -m "Fail clearly when the requested backend cannot be used."
```

### Task 6: Full verification and local Silero activation

**Files:**
- Runtime only: `~/.config/echoflow/echoflow.conf`
- Runtime only: `~/.config/echoflow/vad/silero-v6.2.0-ggml.bin`

- [ ] **Step 1: Run the complete repository verification**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
./build/service/echoflow-service --print-default-config
bash -n install-user.sh uninstall-user.sh tests/spec/*.sh
sh -n run.sh
git diff --check
git status --short
```

Expected: every command succeeds; compiled defaults still report
`vad_backend=energy`; only intended source, test, spec, and plan files appear.

- [ ] **Step 2: Install the verified build without starting it**

Run the repository installer with `--no-start` using approval for writes under
`~/.local`, `~/.config/systemd`, and the user systemd manager:

```bash
./install-user.sh --no-start
```

Expected: the service and UI host install under `~/.local`, units reload, and
the existing EchoFlow configuration remains intact.

- [ ] **Step 3: Install the local Silero model and activate configuration**

Create `~/.config/echoflow/vad`, install
`third_party/crispasr/models/for-tests-silero-v6.2.0-ggml.bin` as
`silero-v6.2.0-ggml.bin`, and verify its SHA-256 against the source file.

Make a timestamped backup of `echoflow.conf`. Copy the configuration to a
temporary workspace file, use `apply_patch` to add or replace these sections,
then install the patched file back with mode `0600`:

```ini
[advanced.vad.backend]
value=silero
[advanced.vad.model]
value=$HOME/.config/echoflow/vad/silero-v6.2.0-ggml.bin
[advanced.vad.threshold]
value=0.10
[advanced.vad.min_speech_ms]
value=100
[advanced.vad.min_silence_ms]
value=300
```

Do not enable `save_live_debug_audio` and do not create a recording.

- [ ] **Step 4: Validate configuration and restart the user service**

Run:

```bash
~/.local/bin/echoflow-service --config ~/.config/echoflow/echoflow.conf --self-test
systemctl --user daemon-reload
systemctl --user restart echoflow.service
systemctl --user show echoflow.service -p ActiveState -p SubState -p ExecStart
journalctl --user -u echoflow.service -n 80 --no-pager
```

Expected: self-test reports the Silero model available, the service is active,
`ExecStart` resolves to the newly installed binary, and startup has no model or
configuration error.

- [ ] **Step 5: Prove the live backend and hand off the manual trial**

Trigger one short recording through the normal EchoFlow UI only if the user is
present to speak; otherwise stop after service verification and ask the user to
record a sentence, pause naturally, and stop. Then inspect:

```bash
journalctl --user -u echoflow.service --since '5 minutes ago' --no-pager \
  | rg 'live VAD backend|live VAD silero|live segment queued|pipeline failed'
```

Expected: `live VAD backend: silero` appears and a natural pause queues a
segment before `audio_ms=8000`. Report any missing short utterance, false
activation, or stop-latency regression rather than tuning blindly.

- [ ] **Step 6: Record the clean final state**

Run:

```bash
git status --short
git log --oneline --decorate -8
```

Expected: no build artifacts, model files, recordings, or local configuration
are staged. Report the commit list, verification evidence, active settings,
backup path, and rollback instruction (`backend=energy` plus service restart).
