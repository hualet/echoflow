# Live Silero Single Silence Gate Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Emit live Silero segments on the first analysis where the backend has already closed the speech interval, without applying a second endpoint-silence duration in the live adapter.

**Architecture:** Keep `advanced.vad.min_silence_ms` owned by `SileroVadBackend`, which converts a still-open utterance to an interval ending at the current buffer and a confirmed endpoint to an interval ending before it. `SileroLiveSegmenter` will use that open/closed interval contract directly while preserving pre/post padding, forced splitting, flush, absolute offsets, and the energy-VAD path.

**Tech Stack:** C++17, CMake, Qt6 QTest, vendored CrispASR Silero VAD, systemd user services.

---

## File Map

- Modify `tests/test_silero_live_segmenter.cpp`: add a fake backend that follows
  Silero's open/closed interval contract and a focused latency regression.
- Modify `service/SileroLiveSegmenter.h`: remove the adapter-owned endpoint
  silence setting and cached sample count.
- Modify `service/SileroLiveSegmenter.cpp`: treat `end < buffer_.size()` as a
  backend-confirmed natural endpoint and retain `end == buffer_.size()` as open.
- Modify `service/LiveSegmenterFactory.cpp`: continue passing minimum silence to
  `SileroVadBackend`, but stop duplicating it in the live adapter.
- Do not modify `Config`, energy VAD, ASR, text joining, Fcitx, UI, or debug-audio
  behavior.

### Task 1: Honor Backend-Confirmed Silero Endpoints

**Files:**
- Modify: `tests/test_silero_live_segmenter.cpp`
- Modify: `service/SileroLiveSegmenter.h`
- Modify: `service/SileroLiveSegmenter.cpp`
- Modify: `service/LiveSegmenterFactory.cpp`

- [ ] **Step 1: Add a fake backend that models Silero's interval contract**

Add this backend beside the existing test backends in
`tests/test_silero_live_segmenter.cpp`. The constants describe `200 ms` of
leading silence, `800 ms` of speech, CrispASR's `30 ms` speech padding, and a
backend-confirmed endpoint after `300 ms` of silence:

```cpp
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
```

This fake intentionally returns `end == count` while the utterance is open.
Once the backend has confirmed its own `300 ms` silence, it returns the padded
closed end before `count`.

- [ ] **Step 2: Write the regression test**

Register a new test slot:

```cpp
void backendConfirmedSilenceEmitsImmediately();
```

Rename the existing slot so it describes the backend contract rather than the
removed adapter timer:

```cpp
void unfinishedIntervalWaitsForBackendClosure();
```

Replace the existing `unfinishedIntervalWaitsForEndpointSilence()` body with:

```cpp
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
```

At `1200 ms`, the fake backend has not yet confirmed its `300 ms` endpoint and
returns `end == count`, so the adapter must continue waiting.

Add the test implementation:

```cpp
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
```

The expected end is the backend's padded speech end plus the existing `200 ms`
adapter post-padding, bounded by the `1300 ms` input.

- [ ] **Step 3: Build and run the focused test to prove the regression**

Run:

```bash
cmake --build build --target test_silero_live_segmenter -j2
./build/tests/test_silero_live_segmenter backendConfirmedSilenceEmitsImmediately
```

Expected: the build succeeds and the new test fails because the current
adapter sees only `270 ms` after the padded backend end and waits for its own
`300 ms` gate. QTest reports `segments.size()` as `0` instead of `1`.

- [ ] **Step 4: Remove endpoint-silence ownership from the live adapter**

In `service/SileroLiveSegmenter.h`, reduce the config to cadence, minimum
speech/history, padding, maximum duration, and forced overlap:

```cpp
struct SileroLiveSegmenterConfig {
    int sampleRate = 16000;
    int analysisCadenceMs = 100;
    int minSpeechMs = 100;
    int prePaddingMs = 200;
    int postPaddingMs = 200;
    int maxSegmentMs = 8000;
    int forceOverlapMs = 500;
};
```

Remove this private member:

```cpp
size_t endSilenceSamples_ = 0;
```

In the `SileroLiveSegmenter` initializer list, remove:

```cpp
, endSilenceSamples_(samplesForMs(config_.endSilenceMs))
```

Keep duration validation focused on values still owned by this class:

```cpp
if (analysisCadenceSamples_ == 0 || config_.minSpeechMs <= 0
    || maxSegmentSamples_ == 0) {
    throw std::invalid_argument("Silero live VAD durations must be positive");
}
```

Replace the second duration check in `analyze()`:

```cpp
if (buffer_.size() - end < endSilenceSamples_) continue;
```

with the backend interval-state check:

```cpp
if (end >= buffer_.size()) continue;
```

The equality case is important: `SileroVadBackend` normalizes an open interval
to the current PCM count, so it must remain buffered until the backend closes
it or the eight-second safety split fires.

- [ ] **Step 5: Keep minimum silence only in the backend factory call**

In `service/LiveSegmenterFactory.cpp`, remove:

```cpp
live.endSilenceMs = config.vadMinSilenceMs;
```

Keep the existing backend construction unchanged:

```cpp
auto backend = std::make_unique<SileroVadBackend>(
    config.vadModelPath, config.vadThreshold,
    config.vadMinSpeechMs, config.vadMinSilenceMs);
```

This leaves `advanced.vad.min_silence_ms` validated by `Config` and enforced
once by CrispASR through `SileroVadBackend`.

- [ ] **Step 6: Run focused endpoint and factory tests**

Run:

```bash
cmake --build build --target test_silero_live_segmenter test_live_segmenter_factory -j2
./build/tests/test_silero_live_segmenter
./build/tests/test_live_segmenter_factory
```

Expected: both QTest binaries report zero failures. Existing tests prove that
an interval ending exactly at the current buffer still waits, continuous
speech still force-splits, and flush/reset/de-duplication remain intact.

- [ ] **Step 7: Commit the focused source and test change**

Run:

```bash
git status --short
git add service/SileroLiveSegmenter.h \
  service/SileroLiveSegmenter.cpp \
  service/LiveSegmenterFactory.cpp \
  tests/test_silero_live_segmenter.cpp
git diff --cached --check
git commit -m "fix(service): avoid rechecking Silero endpoint silence" \
  -m "Silero already closes intervals after the configured minimum
silence, but the live adapter waits on a second duration gate.

Treat backend-closed intervals as final immediately while preserving
open intervals, padding, flush, and forced splitting."
```

Expected: one Conventional Commit containing only the four listed files. The
body explains both the user-visible delay and the single-owner fix.

### Task 2: Verify the Change Against the Repository

**Files:**
- Verify only; no repository files should change.

- [ ] **Step 1: Configure and build the complete project**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j2
```

Expected: configuration and all targets complete without compiler or linker
errors.

- [ ] **Step 2: Run the complete automated test suite**

Run:

```bash
ctest --test-dir build --output-on-failure
```

Expected: all tests pass; the current branch baseline contains 26 tests.

- [ ] **Step 3: Run service and script sanity checks**

Run:

```bash
./build/service/echoflow-service --print-default-config
./build/service/echoflow-service --self-test
bash -n install-user.sh uninstall-user.sh tests/spec/*.sh
sh -n run.sh
```

Expected: the default config still prints `energy`, self-test succeeds, and
all shell parsers exit zero.

- [ ] **Step 4: Confirm scope and a clean committed tree**

Run:

```bash
git diff --check HEAD^
git status --short
git show --stat --oneline HEAD
```

Expected: no whitespace errors, no uncommitted files, and the implementation
commit changes only the four files from Task 1.

### Task 3: Validate the Real Backend and Restart the Trial Service

**Files:**
- Create only under ignored `build/silero-single-gate-probe/`: generated WAV,
  manifest, and JSONL output.
- Modify only local installed artifacts through `install-user.sh`.
- Do not record microphone audio or modify tracked files.

- [ ] **Step 1: Generate a public-sample pause probe**

Create `build/silero-single-gate-probe/`, then use the vendored public JFK
sample and append one second of generated silence:

```bash
mkdir -p build/silero-single-gate-probe
ffmpeg -hide_banner -loglevel error -y \
  -i ../../third_party/crispasr/samples/jfk.wav \
  -f lavfi -t 1 -i anullsrc=r=16000:cl=mono \
  -filter_complex '[0:a]aresample=16000,aformat=sample_fmts=s16:channel_layouts=mono[a0];[a0][1:a]concat=n=2:v=0:a=1[out]' \
  -map '[out]' -ar 16000 -ac 1 \
  build/silero-single-gate-probe/jfk-with-pause.wav
sha256sum build/silero-single-gate-probe/jfk-with-pause.wav
```

Expected: ffmpeg exits zero and produces a 16 kHz mono WAV. No private or
microphone audio is used.

- [ ] **Step 2: Run the real Silero backend with the unchanged trial values**

Use `apply_patch` to create
`build/silero-single-gate-probe/manifest.json` with this exact content. The WAV
path is relative to the manifest. The all-zero hash is an explicit local-probe
sentinel because the benchmark records but does not validate this metadata;
the preceding `sha256sum` remains the actual artifact identity:

```json
[
  {
    "id": "public-jfk-with-pause",
    "audio": "jfk-with-pause.wav",
    "sha256": "0000000000000000000000000000000000000000000000000000000000000000",
    "reference": "",
    "speech": [],
    "condition": "public-sample-with-generated-pause"
  }
]
```

Run:

```bash
./build/tests/vad_replay_benchmark \
  --manifest build/silero-single-gate-probe/manifest.json \
  --backend silero \
  --vad-model ../../third_party/crispasr/models/for-tests-silero-v6.2.0-ggml.bin \
  --threshold 0.15 \
  --end-silence-ms 300 \
  > build/silero-single-gate-probe/result.jsonl
```

Expected: exit zero, a Silero interval ending before the generated file end,
and no inference failure. Together with Task 1's live-adapter regression, this
proves the real backend supplies the closed-interval state consumed by the new
single-gate logic.

- [ ] **Step 3: Capture the pre-install service identity**

Run:

```bash
systemctl --user show echoflow.service \
  -p MainPID -p ExecStart -p ActiveEnterTimestamp -p NRestarts
sha256sum ./build/service/echoflow-service \
  /home/hualet/.local/bin/echoflow-service
```

Expected: record the old PID and both hashes so the restart and installed
binary can be verified rather than assumed.

- [ ] **Step 4: Install and restart through the repository workflow**

Run:

```bash
./install-user.sh
```

Expected: the script builds and installs user artifacts, reloads systemd, and
restarts `echoflow.service` and `echoflow-ui.service` successfully. It must not
overwrite the existing local VAD selection or enable debug-audio recording.

- [ ] **Step 5: Verify the running binary, PID, and Silero configuration**

Run:

```bash
systemctl --user show echoflow.service \
  -p MainPID -p ExecStart -p ActiveEnterTimestamp -p NRestarts
systemctl --user is-active echoflow.service echoflow-ui.service
sha256sum ./build/install-user/service/echoflow-service \
  /home/hualet/.local/bin/echoflow-service
rg -n 'advanced.vad.(backend|threshold|min_speech_ms|min_silence_ms)|value=' \
  /home/hualet/.config/echoflow/echoflow.conf
journalctl --user -u echoflow.service --since '-2 minutes' --no-pager \
  | rg 'live VAD backend|Silero|error|failed'
```

Expected: a new nonzero PID, both services active, matching built/installed
service hashes, `silero / 0.15 / 100 / 300` in the local configuration, a log
line proving the Silero live backend was selected, and no startup error.

- [ ] **Step 6: Hand off a manual pause trial without recording private audio**

Ask the user to dictate one phrase, pause naturally for at least one second
without releasing right Ctrl, and observe whether text appears before stop.
After the trial, inspect only service timing logs:

```bash
journalctl --user -u echoflow.service --since '-5 minutes' --no-pager \
  | rg 'Silero VAD boundary|queued live segment|live segment ASR finished|live partial emitted|queue peak|error|failed'
```

Acceptance requires `reason=natural` and the queued live segment to occur
before the recording-stop event, with no VAD, pipeline, or backpressure error.
The ASR result may still follow by roughly its decode time; this fix removes
the adapter's duplicate endpoint gate but does not change ASR decoding.
