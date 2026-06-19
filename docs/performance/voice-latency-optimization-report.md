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
