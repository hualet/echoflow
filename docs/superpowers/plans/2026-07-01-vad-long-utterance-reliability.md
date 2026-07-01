# VAD and Long-Utterance Reliability Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prevent live-capture loss, improve voice endpointing, and prove lower VAD misses, latency, and long-dictation omissions in a reproducible report.

**Architecture:** Keep PipeWire capture continuously draining into ordered PCM blocks, perform segmentation independently, and send completed segments to one ASR worker that owns `CrispSession`. Add a small VAD backend boundary so the existing energy detector remains a fallback while a packaged Silero model is evaluated under the same replay harness.

**Tech Stack:** C++17, Qt6 QTest, CrispASR C ABI, PipeWire `pw-record`, CMake/CTest, JSON-lines benchmark output.

---

## File Map

- Create `service/SegmentAsrWorker.h/.cpp`: ordered segment queue and single ASR thread.
- Create `service/LiveDebugRecorder.h/.cpp`: opt-in PCM WAV and JSON sidecar output.
- Create `service/VadBackend.h`: backend-neutral speech interval interface.
- Create `service/SileroVadBackend.h/.cpp`: CrispASR Silero adapter for replay and endpoint decisions.
- Create `service/TextJoiner.h/.cpp`: conservative overlap removal at forced boundaries.
- Modify `service/CrispLiveVoicePipeline.h/.cpp`: capture/ASR separation, lifecycle, metrics, and backend selection.
- Modify `service/AudioSegmenter.h/.cpp`: configurable longer maximum and low-energy split metadata.
- Modify `service/Config.h/.cpp`, `service/ModelCatalog.h`, `service/SelfTest.cpp`: VAD configuration and offline model validation.
- Modify `service/CMakeLists.txt`, `CMakeLists.txt`: compile the new units and only the CrispASR VAD dependencies needed by EchoFlow.
- Create `tests/test_segment_asr_worker.cpp`, `tests/test_text_joiner.cpp`, `tests/test_vad_backend.cpp`.
- Extend `tests/test_audio_segmenter.cpp`, `tests/test_config.cpp`, `tests/test_model_catalog.cpp`.
- Create `tests/benchmarks/vad_replay_benchmark.cpp`: replay, label, and metric output.
- Create `tests/data/vad/manifest.example.json`: documented local dataset schema without private audio.
- Create `docs/performance/vad-long-utterance-evaluation.md`: baseline/candidate results and decision.

### Task 1: Establish Sample-Preservation and Queue Baseline

**Files:**
- Create: `service/SegmentAsrWorker.h`
- Create: `service/SegmentAsrWorker.cpp`
- Create: `tests/test_segment_asr_worker.cpp`
- Modify: `service/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write a failing slow-ASR ordering test**

Define `SegmentAsrWorker` around an injected function so tests do not load a model:

```cpp
SegmentAsrWorker worker([](const AudioSegment& segment) {
    QThread::msleep(80);
    return std::to_string(segment.samples.front());
});
worker.start();
for (int i = 0; i < 12; ++i) {
    AudioSegment segment;
    segment.samples = {static_cast<int16_t>(i)};
    QVERIFY(worker.enqueue(std::move(segment)));
}
QCOMPARE(worker.finish(), std::vector<std::string>({"0", "1", "2", "3", "4", "5",
                                                   "6", "7", "8", "9", "10", "11"}));
QCOMPARE(worker.enqueuedCount(), size_t(12));
QCOMPARE(worker.completedCount(), size_t(12));
```

- [ ] **Step 2: Run the focused test and verify RED**

Run: `cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build --target test_segment_asr_worker -j 8 && ./build/tests/test_segment_asr_worker`

Expected: build fails because `SegmentAsrWorker` does not exist.

- [ ] **Step 3: Implement the ordered worker**

Expose this lifecycle:

```cpp
class SegmentAsrWorker {
public:
    using Transcribe = std::function<std::string(const AudioSegment&)>;
    explicit SegmentAsrWorker(Transcribe transcribe);
    ~SegmentAsrWorker();
    void start();
    bool enqueue(AudioSegment segment);
    std::vector<std::string> finish();
    void cancel();
    size_t enqueuedCount() const;
    size_t completedCount() const;
    size_t highWaterMark() const;
};
```

Use one mutex, condition variable, FIFO deque, and one worker thread. `finish()`
closes input and drains the queue. `cancel()` clears queued segments and returns
no text. Catch transcribe exceptions, record the first error, continue draining,
and make `finish()` throw so incomplete output cannot be committed.

- [ ] **Step 4: Run the worker and existing tests**

Run: `cmake --build build --target test_segment_asr_worker -j 8 && ./build/tests/test_segment_asr_worker && ctest --test-dir build --output-on-failure`

Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add service/SegmentAsrWorker.* service/CMakeLists.txt tests/test_segment_asr_worker.cpp tests/CMakeLists.txt
git commit -m "feat(service): queue live segments for ASR" -m "Move segment decoding behind an ordered worker so slow inference cannot block audio capture. Preserve cancellation and surface incomplete decode failures."
```

### Task 2: Decouple Live Capture from ASR

**Files:**
- Modify: `service/CrispLiveVoicePipeline.h`
- Modify: `service/CrispLiveVoicePipeline.cpp`
- Test: `tests/test_segment_asr_worker.cpp`

- [ ] **Step 1: Add a failing sustained-input test**

Add a coordinator-level test seam that feeds PCM blocks without PipeWire and
uses a transcriber delayed longer than the feed interval. Feed 40 one-second
blocks, then assert:

```cpp
QCOMPARE(metrics.inputSamples, uint64_t(40 * 16000));
QCOMPARE(metrics.segmentSamples, metrics.inputSamples);
QVERIFY(metrics.asrQueueHighWaterMark > 1);
QVERIFY(!metrics.audioDropped);
```

- [ ] **Step 2: Run the focused test and verify RED**

Run: `cmake --build build --target test_segment_asr_worker -j 8 && ./build/tests/test_segment_asr_worker`

Expected: the synchronous pipeline cannot accept blocks while ASR is delayed.

- [ ] **Step 3: Wire `SegmentAsrWorker` into the live pipeline**

Construct the worker after loading `CrispSession`:

```cpp
asrWorker_ = std::make_unique<SegmentAsrWorker>([this](const AudioSegment& seg) {
    const auto pcm = toFloat32(seg.samples);
    return session_->transcribe(pcm.data(), static_cast<int>(pcm.size()));
});
asrWorker_->start();
```

`readerLoop()` must only decode S16, update sample counters, run lightweight
segmentation, and enqueue completed segments. It must never invoke
`CrispSession::transcribe`. `finish()` must stop/reap `pw-record`, join the
reader, enqueue the flushed tail, drain the ASR worker, join ordered text, then
destroy the session. `cancel()` must stop capture before cancelling the worker.

- [ ] **Step 4: Verify sample preservation and lifecycle**

Run: `cmake --build build -j 8 && ./build/tests/test_segment_asr_worker && ./build/tests/test_voice_session && ctest --test-dir build --output-on-failure`

Expected: all tests pass and the sustained-input test reports zero dropped
samples.

- [ ] **Step 5: Commit**

```bash
git add service/CrispLiveVoicePipeline.* tests/test_segment_asr_worker.cpp
git commit -m "fix(service): keep capture running during ASR" -m "Drain PipeWire continuously while queued segments decode on a dedicated worker. Wait for ordered results at finish so long dictation cannot silently lose buffered audio."
```

### Task 3: Make Live Failures Replayable

**Files:**
- Create: `service/LiveDebugRecorder.h`
- Create: `service/LiveDebugRecorder.cpp`
- Modify: `service/CrispLiveVoicePipeline.h`
- Modify: `service/CrispLiveVoicePipeline.cpp`
- Modify: `service/CMakeLists.txt`
- Test: `tests/test_segment_asr_worker.cpp`

- [ ] **Step 1: Write a failing debug artifact test**

With a temporary recordings directory and `saveLiveDebugAudio=true`, feed
32,000 known samples and assert the WAV data chunk is exactly 64,000 bytes. Parse
the JSON sidecar and assert `captured_samples=32000`, segment ranges are ordered,
and `audio_dropped=false`.

- [ ] **Step 2: Run the test and verify RED**

Run: `cmake --build build --target test_segment_asr_worker -j 8 && ./build/tests/test_segment_asr_worker`

Expected: no WAV or sidecar is produced.

- [ ] **Step 3: Implement buffered WAV and sidecar output**

`LiveDebugRecorder` accepts PCM blocks and metric events. Write a placeholder
44-byte PCM WAV header, append S16 samples, and patch RIFF/data lengths in
`finish()`. Emit one JSON object containing sample counts, segment ranges,
boundary reasons, queue high-water mark, per-segment ASR time, errors, and final
text. Never create artifacts unless the opt-in flag is true.

- [ ] **Step 4: Run focused and config tests**

Run: `cmake --build build --target test_segment_asr_worker test_config -j 8 && ./build/tests/test_segment_asr_worker && ./build/tests/test_config`

Expected: exact sample and privacy-default assertions pass.

- [ ] **Step 5: Commit**

```bash
git add service/LiveDebugRecorder.* service/CrispLiveVoicePipeline.* service/CMakeLists.txt tests/test_segment_asr_worker.cpp
git commit -m "feat(service): record live transcription diagnostics" -m "Connect the existing opt-in setting to exact PCM and segmentation metadata so VAD and omission failures can be replayed without enabling recording by default."
```

### Task 4: Add a Reproducible VAD Replay Benchmark

**Files:**
- Create: `tests/benchmarks/vad_replay_benchmark.cpp`
- Create: `tests/data/vad/manifest.example.json`
- Modify: `tests/CMakeLists.txt`
- Create: `docs/performance/vad-long-utterance-evaluation.md`

- [ ] **Step 1: Define and test the manifest parser and interval metrics**

Use a manifest entry shaped as:

```json
{"audio":"quiet-zh.wav","reference":"你好，这是轻声测试", "speech":[[0.42,2.71]],"condition":"quiet"}
```

Given labeled and predicted intervals, test exact missed-speech duration, false
activation duration, endpoint delay, and segment count. Keep private WAV files
out of git.

- [ ] **Step 2: Run the benchmark target and verify RED**

Run: `cmake --build build --target vad_replay_benchmark -j 8`

Expected: target does not exist.

- [ ] **Step 3: Implement JSON-lines benchmark output**

Support:

```text
vad_replay_benchmark --manifest FILE --backend energy --config FILE
```

Output one JSON object per clip plus a summary containing speech miss rate,
false activation rate, median endpoint delay, CER, duplication count, decode
RTF, and stop latency. Include transcript text and segment sample ranges.

- [ ] **Step 4: Capture the authoritative current baseline**

Run the energy backend against every available labeled local recording and add
the command, machine description, per-condition summary, and raw result path to
`docs/performance/vad-long-utterance-evaluation.md`. Mark missing 30/60-second
cases explicitly; do not fabricate measurements.

- [ ] **Step 5: Commit**

```bash
git add tests/benchmarks/vad_replay_benchmark.cpp tests/data/vad/manifest.example.json tests/CMakeLists.txt docs/performance/vad-long-utterance-evaluation.md
git commit -m "test(vad): add replay baseline benchmark" -m "Measure VAD misses, false activation, endpoint delay, transcript completeness, and decode latency on identical labeled recordings. Preserve the current energy path as the comparison baseline."
```

### Task 5: Integrate Offline Silero VAD Behind a Backend Boundary

**Files:**
- Create: `service/VadBackend.h`
- Create: `service/SileroVadBackend.h`
- Create: `service/SileroVadBackend.cpp`
- Modify: `CMakeLists.txt`
- Modify: `service/CMakeLists.txt`
- Modify: `service/Config.h`
- Modify: `service/Config.cpp`
- Modify: `service/ModelCatalog.h`
- Modify: `service/SelfTest.cpp`
- Modify: `tests/test_config.cpp`
- Modify: `tests/test_model_catalog.cpp`
- Create: `tests/test_vad_backend.cpp`

- [ ] **Step 1: Prove the narrow CrispASR VAD build surface**

Temporarily configure `crispasr-lib` with the Silero VAD sources required by
`crispasr_vad_segments`, build `test_vad_backend`, and inspect unresolved
symbols. Keep `CRISPASR_QWEN3_ONLY`; do not restore unrelated backends.

- [ ] **Step 2: Write failing adapter and configuration tests**

Assert defaults and parsing for:

```ini
[advanced.vad.backend]
value=silero
[advanced.vad.model]
value=$HOME/.config/echoflow/vad/ggml-silero-v6.2.0.bin
```

Assert a missing neural model selects energy fallback with an explicit status,
while a corrupt model returns an error instead of pretending that no speech
was found.

- [ ] **Step 3: Implement `IVadBackend` and the Silero adapter**

Use this stable boundary:

```cpp
struct SpeechInterval { size_t beginSample; size_t endSample; float confidence; };
class IVadBackend {
public:
    virtual ~IVadBackend() = default;
    virtual std::vector<SpeechInterval> detect(const int16_t* pcm, size_t count) = 0;
    virtual std::string name() const = 0;
};
```

Convert S16 to F32 and call `crispasr_vad_segments` with the packaged model.
Validate every returned span, clamp it to the input sample count, copy results,
and free the C allocation with `crispasr_vad_free`.

- [ ] **Step 4: Package and validate the model without service downloads**

Add the VAD model as an explicit catalog/download artifact with checksum-aware
download behavior in the existing UI host. Extend `SelfTest` to report the
exact missing path and selected fallback. The service itself must never access
HTTP.

- [ ] **Step 5: Run adapter, catalog, config, and self-tests**

Run: `cmake --build build --target test_vad_backend test_model_catalog test_config test_selftest -j 8 && ./build/tests/test_vad_backend && ./build/tests/test_model_catalog && ./build/tests/test_config && ./build/tests/test_selftest`

Expected: valid fixture intervals pass, corrupt model fails visibly, and missing
model fallback is explicit.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt service/VadBackend.h service/SileroVadBackend.* service/CMakeLists.txt service/Config.* service/ModelCatalog.h service/SelfTest.cpp tests/test_vad_backend.cpp tests/test_config.cpp tests/test_model_catalog.cpp tests/CMakeLists.txt ui-host
git commit -m "feat(vad): add offline Silero backend" -m "Expose CrispASR Silero detection through a narrow adapter and keep the energy detector as an explicit fallback. Validate the offline model through catalog and self-test paths."
```

### Task 6: Improve Endpointing and Forced Boundaries

**Files:**
- Modify: `service/AudioSegmenter.h`
- Modify: `service/AudioSegmenter.cpp`
- Create: `service/TextJoiner.h`
- Create: `service/TextJoiner.cpp`
- Modify: `service/CrispLiveVoicePipeline.cpp`
- Modify: `service/CMakeLists.txt`
- Modify: `tests/test_audio_segmenter.cpp`
- Create: `tests/test_text_joiner.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add failing boundary tests**

Verify natural silence is preferred, continuous speech reaches 15 seconds, a
forced split retains configured overlap, and no input samples disappear across
the union of adjacent segments. For text, verify exact suffix/prefix overlap is
removed but near-matches are retained:

```cpp
QCOMPARE(joinOverlappingText("今天我们继续讨论语音输入", "讨论语音输入的完整性"),
         std::string("今天我们继续讨论语音输入的完整性"));
QCOMPARE(joinOverlappingText("输入可能丢字", "输入不应该丢字"),
         std::string("输入可能丢字 输入不应该丢字"));
```

- [ ] **Step 2: Run focused tests and verify RED**

Run: `cmake --build build --target test_audio_segmenter test_text_joiner -j 8 && ./build/tests/test_audio_segmenter && ./build/tests/test_text_joiner`

Expected: 15-second overlap and join behavior are absent.

- [ ] **Step 3: Implement conservative boundary handling**

Raise the default maximum to 15 seconds. Track recent frame decisions and split
at the lowest-confidence frame in a bounded lookback when the selected backend
provides confidence. Otherwise force split with 500 ms overlap. Deduplicate only
an exact UTF-8 code-point suffix/prefix of at least two Chinese characters or
one complete whitespace-delimited word; never fuzzy-delete differing text.

- [ ] **Step 4: Run segment, join, and pipeline tests**

Run: `cmake --build build --target test_audio_segmenter test_text_joiner test_segment_asr_worker -j 8 && ./build/tests/test_audio_segmenter && ./build/tests/test_text_joiner && ./build/tests/test_segment_asr_worker`

Expected: sample coverage, conservative joining, and queue ordering pass.

- [ ] **Step 5: Commit**

```bash
git add service/AudioSegmenter.* service/TextJoiner.* service/CrispLiveVoicePipeline.cpp service/CMakeLists.txt tests/test_audio_segmenter.cpp tests/test_text_joiner.cpp tests/CMakeLists.txt
git commit -m "fix(vad): preserve context across long speech" -m "Prefer natural endpoints, extend continuous-speech windows, and retain overlap at forced boundaries. Remove only exact repeated text so joins do not hide omissions."
```

### Task 7: Select the Production Backend from Measurements

**Files:**
- Modify: `tests/data/vad/manifest.example.json`
- Modify: `docs/performance/vad-long-utterance-evaluation.md`
- Modify: `service/Config.h`
- Modify: `ui-host/EchoFlowSettings.cpp`
- Modify: `ui-host/settings-schema.json`

- [ ] **Step 1: Run identical energy and Silero replay sweeps**

Run each backend at least three times on the same manifest. Record raw JSONL and
report median/dispersion for VAD miss rate, false activation, endpoint delay,
CER, boundary omission/duplication, first stable text, stop latency, and RTF.

- [ ] **Step 2: Tune one variable at a time**

Evaluate speech threshold, minimum speech, minimum silence, padding, and maximum
segment length independently. Keep rejected settings and their regressions in
the report. Do not select a neural default unless it passes every acceptance
gate from the design.

- [ ] **Step 3: Set the measured production default**

If Silero passes, default to `silero` with energy fallback. If it does not,
retain `energy` and document why; the queue fix still ships because it addresses
the independently proven audio-loss defect.

- [ ] **Step 4: Commit**

```bash
git add docs/performance/vad-long-utterance-evaluation.md service/Config.h ui-host/EchoFlowSettings.cpp ui-host/settings-schema.json
git commit -m "perf(vad): select measured endpoint defaults" -m "Choose the production VAD and endpoint settings from repeated replay results. Preserve rejected configurations and regressions in the evaluation report."
```

### Task 8: Installed Runtime and Final Report Verification

**Files:**
- Modify: `docs/performance/vad-long-utterance-evaluation.md`

- [ ] **Step 1: Run repository verification**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j 8
ctest --test-dir build --output-on-failure
bash -n install-user.sh uninstall-user.sh tests/spec/*.sh
sh -n run.sh
./build/service/echoflow-service --print-default-config
./build/service/echoflow-service --self-test
```

Expected: all commands pass and default configuration reports the selected VAD
backend and model status.

- [ ] **Step 2: Validate installed live behavior**

Install with `./install-user.sh`, restart `echoflow.service` and
`echoflow-ui.service`, reload Fcitx with `fcitx5 -rd`, then record short, quiet,
30-second, and 60-second dictation. Save opt-in debug artifacts and verify their
sample counts, committed order, and journal errors.

- [ ] **Step 3: Complete the evaluation report**

The report must contain environment, commit IDs, dataset manifest, commands,
raw result locations, baseline/candidate tables, acceptance-gate verdicts,
negative findings, privacy handling, and remaining limitations. It must state
which evidence proves VAD, latency, and omission improvement independently.

- [ ] **Step 4: Audit the design requirement by requirement**

Cross-check every scope item, metric, acceptance criterion, test class, and
delivery step in
`docs/superpowers/specs/2026-07-01-vad-long-utterance-reliability-design.md`.
Treat missing 30/60-second evidence or unlabelled audio as incomplete.

- [ ] **Step 5: Commit the final evidence**

```bash
git add docs/performance/vad-long-utterance-evaluation.md
git commit -m "docs(performance): report VAD reliability results" -m "Compare baseline and candidate capture, endpointing, recognition completeness, and latency on identical recordings. Record acceptance verdicts and remaining limitations."
```
