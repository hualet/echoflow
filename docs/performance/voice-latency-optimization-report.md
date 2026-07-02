# Voice Latency Benchmark And Optimization Report

## Summary

The slow user-visible path is the second right-Ctrl press: `VoiceSession` stops
recording, runs qwen-asr, and commits text synchronously before replying. A
synthetic benchmark shows the service state-machine and commit plumbing are
effectively free compared with ASR. A real qwen-asr benchmark shows the first
recognition also paid model loading cost.

Implemented optimizations:

- `echoflow-service` now preloads qwen-asr at service startup through
  `AsrEngine::preload()`. This moves about 0.8-1.1 seconds out of the first
  stop-to-text interaction on this machine.
- `AsrEngine` sets OpenBLAS to 4 threads before loading qwen-asr. Real short
  dictation samples improved from roughly 2.0-2.7 seconds with one OpenBLAS
  thread to roughly 1.5-2.1 seconds with four threads.

Follow-up comparison against the previous Python/GGUF runner found one major
optimization in that stack: the GGUF `asr.py` pads every final short chunk to 40
seconds. Removing that pad makes the old Python engine competitive with, and in
some runs faster than, the current qwen-asr-c path for short dictation.

## Benchmark Design

Added `tests/benchmarks/voice_latency_benchmark.cpp`, built as
`build/tests/voice_latency_benchmark`.

Modes:

- `--session-synthetic`: measures `VoiceSession` stop-to-reply overhead with
  fake recorder, ASR, committer, and UI.
- `--transcribe-file FILE`: measures real `AsrEngine` behavior on a WAV file.
- `--no-preload`: simulates the pre-optimization cold first transcription.
- `--model-dir PATH`: benchmark-only override for local model placement. This
  does not reintroduce a `model_dir` config key.
- `--openblas-threads N`: compares OpenBLAS thread counts without editing the
  user config.
- `--threads N`, `--skip-silence`, `--stream`: benchmark-only qwen-asr runtime
  knobs used to rule out higher-risk defaults.

The benchmark prints one JSON object per iteration.

## Commands Run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target voice_latency_benchmark

ffmpeg -hide_banner -loglevel error \
  -f lavfi -i anullsrc=r=16000:cl=mono \
  -t 2 -acodec pcm_s16le /tmp/echoflow-bench-silence.wav -y

./build/tests/voice_latency_benchmark --session-synthetic --iterations 20

./build/tests/voice_latency_benchmark \
  --transcribe-file /tmp/echoflow-bench-silence.wav \
  --config "$HOME/.config/echoflow/echoflow.conf" \
  --model-dir "$HOME/.config/echoflow/qwen3-asr-0.6b" \
  --iterations 1 --no-preload

./build/tests/voice_latency_benchmark \
  --transcribe-file /tmp/echoflow-bench-silence.wav \
  --config "$HOME/.config/echoflow/echoflow.conf" \
  --model-dir "$HOME/.config/echoflow/qwen3-asr-0.6b" \
  --iterations 3

./build/tests/voice_latency_benchmark \
  --transcribe-file "$HOME/.local/share/echoflow/recordings/voice-20260619-005648.wav" \
  --openblas-threads 1 --iterations 3

./build/tests/voice_latency_benchmark \
  --transcribe-file "$HOME/.local/share/echoflow/recordings/voice-20260619-005648.wav" \
  --openblas-threads 4 --iterations 3
```

## Results

Synthetic session benchmark, 20 iterations:

- `stop_to_reply_ms`: first iteration 0.006 ms, later iterations 0.001 ms.
- `record_stop_ms`: 0.000 ms with fake recorder.
- `transcribe_ms`: 0.000 ms with fake ASR.
- `commit_ms`: 0.000-0.001 ms with fake committer.

Conclusion: the service state machine, UI notifications, and commit interface
dispatch are not the meaningful bottleneck.

Real qwen-asr benchmark used:

- Model: `$HOME/.config/echoflow/qwen3-asr-0.6b`
- Audio: generated 2-second 16 kHz mono PCM silence WAV at
  `/tmp/echoflow-bench-silence.wav`

Cold path without preload:

```json
{"preload_requested":false,"transcribe_ms":4647.154,"chars":6}
```

The service log for that run reported qwen's internal transcription time as
`3.535024s`, so roughly 1.1 seconds of the measured call was model loading and
first-use setup.

Preloaded path:

```json
{"preload_ms":785.752,"preload_requested":true,"transcribe_ms":2250.670,"chars":6}
```

Follow-up preloaded run, 3 iterations:

- preload once: 763.023 ms
- iteration 1: 1914.324 ms
- iteration 2: 1556.483 ms
- iteration 3: 1463.206 ms

Conclusion: preloading removes model load from the user-visible stop-to-text
phase. On this machine the first measured stop-to-text ASR call improved from
about 4.65 seconds to about 2.25 seconds. Subsequent warm calls settled around
1.46-1.91 seconds for the synthetic silence sample.

Real short dictation benchmark, preloaded, qwen internal thread pool left at
default:

| Audio | Duration | OpenBLAS 1 thread | OpenBLAS 4 threads |
| --- | ---: | ---: | ---: |
| `voice-20260619-005149.wav` | 3.84 s | 2474 / 2394 ms | 1862 / 1742 ms |
| `voice-20260619-005209.wav` | 4.95 s | 2681 / 2608 ms | 2103 / 1965 ms |
| `voice-20260619-005648.wav` | 2.36 s | 2110 / 1996 ms | 1672 / 1544 ms |

Rejected runtime knobs:

- qwen internal thread pool (`qwen_set_threads`) was noisy and not reliably
  faster for short dictation. It is not enabled by default.
- `skip_silence` made the measured real samples slower or inconsistent. It is
  not enabled by default.
- qwen streaming transcription was only marginally faster on two samples and
  slower on one sample, without a clear user-visible benefit in the current
  stop-then-transcribe flow. It is not enabled by default.
- Auto language detection was not faster than forcing `Chinese` for the tested
  samples.

## Python/GGUF Comparison

The pre-C++ EchoFlow path was:

```text
echoflow/service.py -> qwen-asr-transcribe subprocess ->
Qwen3-ASR-GGUF/qwen_asr_gguf/inference/asr.py
```

That runner used `$HOME/AI/Model/Qwen3-ASR-GGUF/model-0.6B` with ONNX encoder
files and a `qwen3_asr_llm.q4_k.gguf` decoder. The current qwen-asr-c path uses
`$HOME/.config/echoflow/qwen3-asr-0.6b/model.safetensors`.

The important Python/GGUF finding is in `asr.py`: for each chunk it sliced the
real audio and then padded the final chunk to `chunk_size_sec=40.0`. For
2-5 second dictation, that made the encoder and LLM prefill process a mostly
empty 40 second chunk. The test below removed only that pad in memory with a
runtime monkey patch; the external Python checkout was not modified.

Benchmark environment:

- Same three real EchoFlow recordings under
  `$HOME/.local/share/echoflow/recordings/`.
- Python dependency runner: `uv run --with gguf --with onnxruntime --with
  soundfile --with numpy --with srt`.
- Python engine settings matched the old EchoFlow adapter: ONNX `CPU`,
  `llm_use_gpu=True`, aligner disabled, language forced to `Chinese`.

Measured on 2026-06-19 morning:

| Audio | Duration | qwen-asr-c warm | Python/GGUF pad warm | Python/GGUF no-pad warm | Python/GGUF no-pad cold total |
| --- | ---: | ---: | ---: | ---: | ---: |
| `voice-20260619-005149.wav` | 3.84 s | 1800 / 1714 ms | 4064 ms | 1810 / 1761 ms | 1903 ms |
| `voice-20260619-005209.wav` | 4.95 s | 2008 / 1982 ms | 3844 ms | 2562 / 2354 ms | 1721 ms |
| `voice-20260619-005648.wav` | 2.36 s | 1612 / 1512 ms | 3883 ms | 1411 / 2108 ms | 1394 ms |

Interpretation:

- The no-pad change is real and large. It turns the previous Python/GGUF runner
  from a 3.8-4.1 second warm path into a roughly 1.4-2.6 second warm path on
  these samples.
- qwen-asr-c remains simpler for the product: no Python runtime, no ONNX runtime,
  no llama.cpp shared libraries, no subprocess runner, and one CMake build.
- Pure latency is no longer a decisive win for qwen-asr-c. With the no-pad fix,
  Python/GGUF is in the same range and sometimes faster for short dictation.
- Returning to Python/GGUF as the product path would require intentionally
  reversing the current architecture constraint. A narrower option would be to
  upstream or carry the no-pad fix in the external Python project for comparison
  work, while keeping the shipped EchoFlow service on qwen-asr-c.

## Changes Made

- Added `AsrEngine::preload()` as an explicit API over the existing lazy
  `ensureLoaded()` path.
- `echoflow-service` calls `asr.preload()` before entering the control socket
  loop. Failure is logged through the existing qwen load path and does not exit;
  strict validation remains in `--self-test`.
- Added `preloadReturnsFalseWhenModelCannotLoad` to keep failure behavior
  covered without model weights.
- Added native benchmark tooling under `tests/benchmarks/`.
- Added `advanced.runtime.openblas_threads` with a default of `4`, parsed by the
  service and printed by `--print-default-config`.
- Compared the current qwen-asr-c path with the previous Python/GGUF path and
  verified the old runner's short-audio 40 second pad as the main avoidable
  latency source.

## Remaining Opportunities

1. Add recorder-stop timing with real `pw-record`.
   Current synthetic data cannot prove whether stopping PipeWire adds 10 ms or
   100+ ms on the real desktop path.

2. Consider streaming or chunked ASR only if qwen-asr exposes a stable path for
   this service boundary.
   The measured `qwen_transcribe_stream()` path is not compelling as a drop-in
   replacement. A real streaming UX would need a larger recording, ASR, UI
   progress, and commit data-flow change.

3. Keep model preloading synchronous for now.
   Startup is delayed by about 0.8 seconds in the measured setup, but the
   service avoids a race between background loading and the first transcription.

## 2026-07-02 Release Regression Gate

The Debian 0.2.1 artifact previously took about 11.7 seconds to transcribe the
9.534-second `live-20260621-082316.wav`, while local optimized builds completed
in roughly 2.7-3.3 seconds. The package now selects a deterministic
`x86-64-v3` profile instead of relying on the build host's implicit GGML CPU
target. Debian build logs confirmed these exact flags:

```text
-msse4.2 -mf16c -mfma -mbmi2 -mavx -mavx2
```

The release gate compares optimized binaries, not Debian packages. It reuses a
recorded baseline only when CPU and all benchmark identity fields match. With
no compatible history, it built both `v0.2.1` (`b73bb6d`) and candidate
`3cf0473` on the same AMD Ryzen 7 6800H with GCC 12.3, six CrispASR threads,
four OpenBLAS threads, and the same x86-64-v3 profile.

Input identity:

- model SHA-256: `4c67426908a518c28c24bc780df27175fcf84ce4d6dbd678133a4531904bbcc9`;
- manifest SHA-256: `2b023affdb30112ff78eecd42e90de40a4f3ab73f6111dfb01e8b2e8450cdbb8`;
- config SHA-256: `6ab1c8a70dd86ad80a8c5fbb08f9eb813b334ee773ad1f3316e7281de0a5caaa`;
- five fixed samples: normal dictation, pauses, quiet speech, a short
  interjection, and a deterministic 34.642-second repeated dictation;
- one discarded warm-up followed by three measured iterations per sample.

| Sample | Baseline median | Candidate median | Change | Baseline CER | Candidate CER |
| --- | ---: | ---: | ---: | ---: | ---: |
| normal | 2391.108 ms | 2355.686 ms | -1.48% | 13.79% | 6.90% |
| pauses | 5059.722 ms | 5456.556 ms | +7.84% | 60.38% | 5.66% |
| quiet | 2635.434 ms | 2591.121 ms | -1.68% | 45.83% | 20.83% |
| short | 2563.113 ms | 2662.207 ms | +3.87% | 100.00% | 3200.00% |
| long | 8967.498 ms | 8883.950 ms | -0.93% | 27.14% | 12.86% |

The aggregate median changed from 2635.434 to 2662.207 ms, a 1.02% regression
under the 10% limit. The worst per-sample latency regression was 7.84%, under
the 20% limit. Aggregate CER improved from 37.85% to 28.81%. The formal gate
therefore passed.

One negative finding remains visible: the short `嗯` sample produced
`Transcribe the speech in Chinese` in the candidate, while the baseline was
empty. Aggregate CER still improved enough to satisfy the approved gate, but
this sample-level recognition regression should be investigated separately and
must not be described as a quality improvement.

The actual Debian build passed all 18 tests and installed the expected service,
UI, systemd units, `libechoflow.so`, and addon metadata with
`Library=libechoflow`. Its SHA-256 was
`b4e2da306b1293d3ff867ed58f78ae74031d20498081cdbf61daa82909c2b517`.
An extracted package binary successfully transcribed the 9.534-second normal
sample, confirming the optimized package path no longer reproduces the earlier
11.7-second behavior.

The implementation also preserves no-op incremental builds, adds deterministic
model-free comparison tests, verifies release-only commits in both GitHub
workflows, and rejects a version bump whose evidence does not identify its
candidate parent and previous release baseline.
