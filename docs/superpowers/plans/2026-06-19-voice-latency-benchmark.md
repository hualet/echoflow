# Voice Latency Benchmark Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add reproducible voice latency benchmarks, optimize first-use perceived latency, and write an evidence-backed performance report.

**Architecture:** Keep the existing service/addon/UI-host split. Add timing and benchmark support beside the service logic, then add an explicit qwen-asr preload method used by the service entry point. The benchmark must run without model weights in synthetic mode and with real model/audio when available.

**Tech Stack:** C++17, CMake, Qt6 Test, qwen-asr C API, Unix service code already in `service/`.

---

## File Structure

- Modify `service/AsrEngine.h`: expose `preload()` for service startup and tests.
- Modify `service/AsrEngine.cpp`: implement `preload()` by reusing existing `ensureLoaded()`.
- Modify `service/main.cpp`: call `asr.preload()` before entering the control socket loop.
- Modify `tests/test_asr_engine.cpp`: add compile-time/API coverage for explicit preload failure behavior.
- Create `tests/benchmarks/voice_latency_benchmark.cpp`: native JSONL benchmark for synthetic session timing and real transcribe-file timing.
- Modify `tests/CMakeLists.txt`: build the benchmark executable but do not add it to `ctest` because real mode can require model weights/audio.
- Create `docs/performance/voice-latency-optimization-report.md`: methodology, commands, before/after evidence, and next opportunities.

### Task 1: Add Explicit ASR Preload API

**Files:**
- Modify: `service/AsrEngine.h`
- Modify: `service/AsrEngine.cpp`
- Modify: `tests/test_asr_engine.cpp`

- [ ] **Step 1: Write the failing test**

Add a Qt test slot named `preloadReturnsFalseWhenModelCannotLoad` to `tests/test_asr_engine.cpp`:

```cpp
void preloadReturnsFalseWhenModelCannotLoad();
```

Implementation:

```cpp
void TestAsrEngine::preloadReturnsFalseWhenModelCannotLoad()
{
    Config cfg = Config::defaultConfig();
    cfg.modelDir = "/tmp/echoflow-model-that-does-not-exist";

    AsrEngine engine(cfg);
    QVERIFY(!engine.preload());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_asr_engine && ./build/tests/test_asr_engine`

Expected: compilation fails because `AsrEngine::preload()` is not declared.

- [ ] **Step 3: Write minimal implementation**

In `service/AsrEngine.h`, add:

```cpp
bool preload();
```

In `service/AsrEngine.cpp`, add:

```cpp
bool AsrEngine::preload()
{
    return ensureLoaded();
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_asr_engine && ./build/tests/test_asr_engine`

Expected: test passes, with qwen load failure logs for the missing model path.

### Task 2: Preload Model At Service Startup

**Files:**
- Modify: `service/main.cpp`

- [ ] **Step 1: Add startup preload call**

After constructing `echoflow::AsrEngine asr(cfg);`, call:

```cpp
    asr.preload();
```

Do not exit on failure; the existing transcription path already returns empty text if loading fails, and self-test remains the strict validation mode.

- [ ] **Step 2: Build the service**

Run: `cmake --build build --target echoflow-service`

Expected: build succeeds.

### Task 3: Add Native Voice Latency Benchmark

**Files:**
- Create: `tests/benchmarks/voice_latency_benchmark.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create benchmark source**

Implement a C++ executable that supports:

```text
voice_latency_benchmark --session-synthetic --iterations N
voice_latency_benchmark --transcribe-file FILE [--config PATH] --iterations N
```

It prints one JSON object per iteration with `mode`, `iteration`, and timing fields. Synthetic mode uses fake `IRecorder`, `IAsrEngine`, `ICommitter`, and `IUiNotifier` objects around `VoiceSession`. Real mode constructs `AsrEngine`, calls `preload()`, then times `transcribe()`.

- [ ] **Step 2: Wire benchmark into CMake**

In `tests/CMakeLists.txt`, add:

```cmake
add_executable(voice_latency_benchmark benchmarks/voice_latency_benchmark.cpp)
target_link_libraries(voice_latency_benchmark PRIVATE echoflow_service pthread)
```

- [ ] **Step 3: Build and run synthetic benchmark**

Run:

```bash
cmake --build build --target voice_latency_benchmark
./build/tests/voice_latency_benchmark --session-synthetic --iterations 5
```

Expected: five JSON lines containing `stop_to_reply_ms`, `record_stop_ms`, `transcribe_ms`, and `commit_ms`.

### Task 4: Run Baseline And After Benchmarks

**Files:**
- Create: `docs/performance/voice-latency-optimization-report.md`

- [ ] **Step 1: Run synthetic benchmark**

Run:

```bash
./build/tests/voice_latency_benchmark --session-synthetic --iterations 20
```

Record min/median/max style observations in the report.

- [ ] **Step 2: Run real benchmark if local model/audio are available**

Look for an existing config at `~/.config/echoflow/echoflow.conf` and recordings under `~/.local/share/echoflow/recordings`. If both are present, run:

```bash
./build/tests/voice_latency_benchmark --transcribe-file <wav> --config ~/.config/echoflow/echoflow.conf --iterations 3
```

If either is missing, record that real-model benchmarking was not available in this workspace and keep the synthetic evidence.

- [ ] **Step 3: Write report**

Create `docs/performance/voice-latency-optimization-report.md` with methodology, commands, observed results, implemented preload optimization, and remaining opportunities.

### Task 5: Full Verification

**Files:**
- No new files.

- [ ] **Step 1: Build all targets**

Run: `cmake --build build`

Expected: build succeeds.

- [ ] **Step 2: Run tests**

Run: `ctest --test-dir build --output-on-failure`

Expected: all tests pass.

- [ ] **Step 3: Check worktree scope**

Run: `git status --short`

Expected: only the benchmark, preload, docs, and related CMake/test files are modified.
