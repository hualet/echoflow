# Current CrispASR vs qwen-asr-c Comparison

Date: 2026-06-25

This report compares the current EchoFlow ASR implementation on branch
`test/crispasr-qwen3-gguf` with the previous in-process `qwen-asr-c` backend.
It focuses on the behavior that matters for the product: final-response
latency, live/streaming behavior, VAD behavior, recognition quality, and the
next optimization steps.

## Implementations Compared

Current implementation:

- Final transcription uses `CrispAsrEngine`, which keeps a `CrispSession` in
  process and transcribes WAV files through the CrispASR C API.
- Live voice input uses `CrispLiveVoicePipeline`, which records PCM from
  `pw-record`, runs EchoFlow's `AudioSegmenter` energy VAD, and transcribes each
  completed segment with the warm `CrispSession`.
- The current live path does **not** use CrispASR CLI `--stream-json`, and does
  **not** use CrispASR's FireRed VAD yet.

Previous qwen-asr-c implementation:

- Final transcription used an in-process `qwen-asr-c` context loaded from the
  safetensors model directory.
- It had both normal final decoding and a qwen stream/chunk path
  (`qwen_transcribe_stream`) used by the live experiments.
- Live segmentation also depended on EchoFlow-side segmentation logic, so the
  VAD comparison below is mainly about the current segmenter behavior, not a
  model-VAD replacement.

## Test Setup

- Current worktree: `/home/hualet/projects/hualet/echoflow`
- qwen baseline worktree: `/tmp/echoflow-qwen-baseline` at commit `d1d97f4`
- Current CrispASR model:
  `~/.config/echoflow/qwen3-asr-0.6b/qwen3-asr-0.6b-q4_k.gguf`
- qwen model directory:
  `~/.config/echoflow/qwen3-asr-0.6b`
- Benchmark command:
  `./build*/tests/voice_latency_benchmark --transcribe-file <wav> --config ~/.config/echoflow/echoflow.conf --iterations 3`
- qwen used `--openblas-threads 4`; qwen stream mode additionally used
  `--stream`.
- Numbers below are median wall-clock transcription time after the model was
  preloaded by the benchmark process. The min-max range is from three
  iterations.

## Response Speed

| Clip | Duration | Current CrispASR | qwen final | qwen stream | Current vs qwen final |
| --- | ---: | ---: | ---: | ---: | ---: |
| `jfk.wav` | 11.000 s | 5.698 s, RTF 0.518 | 5.236 s, RTF 0.476 | 4.997 s, RTF 0.454 | 1.09x slower |
| `paraformer_zh.wav` | 13.052 s | 7.323 s, RTF 0.561 | 6.977 s, RTF 0.535 | 6.550 s, RTF 0.502 | 1.05x slower |
| `live-20260621-003152.wav` | 17.321 s | 8.161 s, RTF 0.471 | 6.297 s, RTF 0.364 | 6.514 s, RTF 0.376 | 1.30x slower |
| `live-20260621-004142.wav` | 20.777 s | 10.479 s, RTF 0.504 | 8.192 s, RTF 0.394 | 9.321 s, RTF 0.449 | 1.28x slower |
| `live-20260621-082316.wav` | 9.534 s | 5.737 s, RTF 0.602 | 3.836 s, RTF 0.402 | 5.522 s, RTF 0.579 | 1.50x slower |

Raw ranges:

| Clip | Current CrispASR range | qwen final range | qwen stream range |
| --- | ---: | ---: | ---: |
| `jfk.wav` | 5.055-6.958 s | 5.167-8.034 s | 4.987-5.006 s |
| `paraformer_zh.wav` | 6.477-7.893 s | 6.919-7.081 s | 6.494-6.603 s |
| `live-20260621-003152.wav` | 7.925-8.200 s | 6.253-6.429 s | 6.157-7.138 s |
| `live-20260621-004142.wav` | 10.321-12.259 s | 7.845-8.537 s | 9.101-10.522 s |
| `live-20260621-082316.wav` | 5.677-6.385 s | 3.822-4.235 s | 5.107-5.872 s |

The result is different from the earlier CrispASR CLI cold-load exploration:
under the current in-process integration and same-sample warm benchmark,
qwen-asr-c is still faster on most real EchoFlow recordings. CrispASR is close on
JFK and `paraformer_zh`, but the current integration is not a clear latency win.

## Streaming Behavior

Current CrispASR live behavior:

- The UI gets updates only when `AudioSegmenter` closes a speech segment and the
  warm CrispASR session finishes decoding that segment.
- There is no token-level partial output from the current live path.
- Because final text is produced per completed segment, stop-time latency is
  bounded by the current open segment, but interim feedback depends heavily on
  VAD endpoint timing and per-segment decode time.

Previous qwen-asr-c behavior:

- qwen's stream/chunk path can emit token callbacks from
  `qwen_transcribe_stream`, but the stream output is still tied to chunk decode
  and rollback behavior.
- Earlier streaming-engine tests showed that concatenating low-latency partials
  is not reliable enough to treat as final committed text.
- In the same-sample benchmark above, qwen stream mode is faster than qwen final
  on JFK and `paraformer_zh`, similar on `live-20260621-003152`, and slower than
  qwen final on `live-20260621-004142` and `live-20260621-082316`.

Implication:

Current CrispASR improves the architecture only if we finish the intended native
streaming integration. As implemented today, it is segmented final decoding, not
true streaming ASR.

## VAD Behavior

Current production VAD is still EchoFlow's energy-based `AudioSegmenter`.
Therefore the current CrispASR replacement does not prove better VAD accuracy
than qwen-asr-c; both product paths depend on EchoFlow-side segmentation for live
input.

Observed risks from existing live-recording reports:

- `live-20260621-004142` was segmented into two speech regions in previous
  SenseVoice experiments, which matches the natural pause in the sentence.
- `live-20260621-122709` produced zero VAD segments in the SenseVoice benchmark
  even though qwen decoded `嗯`; short utterances remain the main VAD risk.
- Energy VAD has no semantic confidence, so low-energy speech, mouth clicks,
  microphone gain changes, and short interjections can still be misclassified.

The current CrispASR codebase links CrispASR components that include VAD support,
but the product path is not using FireRed VAD or CrispASR streaming endpointing.
VAD accuracy should therefore be treated as unchanged until a dedicated VAD
backend is wired in and evaluated against labeled live recordings.

## Recognition Accuracy

Representative transcripts:

| Clip | Current CrispASR | qwen-asr-c |
| --- | --- | --- |
| `jfk.wav` | `And so, my fellow Americans, ask not what your country can do for you, ask what you can do for your country` | same text |
| `paraformer_zh.wav` | `正是因为存在绝对正义，所以我们接受现实的相对正义。但是不要因为现实的相对正义，我们就认为这个世界没有正义。因为如果当你认为这个世界没有正义` | same content, with an extra comma after `但是` |
| `live-20260621-003152.wav` | `这次再试一下，我觉得怎么也应该是分段上屏了吧。如果还不是，我觉得绝对是有问题` | same text |
| `live-20260621-004142.wav` | `现在这个算法还依赖麦克风的质量，这样我觉得肯定不太靠谱。我在做第三次测试，但是目前看起来裁分的效果仍然不是特别好` | `现在这个算法还依赖麦克风的质量，这样我觉得肯定不太靠谱。我在做第三次测试，但是目前看起来采分的效果仍然不是特别好` |
| `live-20260621-082316.wav` | `现在试一下这个语音输入，能不能输入，让大包包看一看这个导弹情况` | same content, different punctuation |

Accuracy is effectively tied on this small set. The main caveat is that both
engines truncate `paraformer_zh.wav` at the same point, so the current model
configuration still needs long-utterance testing. On the real recordings, the
only material difference in this subset is `裁分` vs `采分`; the likely intended
word in this debugging context is closer to `拆分/裁分` than `采分`, so CrispASR
is not worse there.

This sample set is too small for a WER/CER claim. It is enough to say that the
current CrispASR integration does not show an obvious accuracy regression on the
sampled recordings, but it also does not prove a broad accuracy improvement over
qwen-asr-c.

## Operational Tradeoffs

Current CrispASR advantages:

- One 515 MiB GGUF model file instead of the larger qwen safetensors model set.
- CMake-native in-process integration after the qwen3-only static embedding
  fixes.
- Clear path to native streaming and neural VAD if those CrispASR APIs are
  exposed through the embedded C API.

Current CrispASR drawbacks:

- In the current warm benchmark, final decoding is slower than qwen-asr-c on the
  real EchoFlow recordings tested here.
- The product path does not yet use the native CrispASR streaming/VAD design
  that motivated the replacement.
- Static linking currently depends on section garbage collection for test and
  benchmark targets because the embedded CrispASR library still contains unused
  optional modules.

qwen-asr-c advantages:

- Faster final decoding on most of the sampled real EchoFlow recordings.
- Existing stream/chunk path is already integrated with token callbacks.
- Smaller integration surface inside EchoFlow because it was purpose-built for
  Qwen ASR only.

qwen-asr-c drawbacks:

- Larger and more fragmented model directory.
- More architecture-specific performance tuning through OpenBLAS and qwen
  kernels.
- The streaming path still did not solve stable final text by itself; segmenting
  and finalizing completed speech remained necessary.

## Optimization Plan

1. Restore native streaming evaluation for the embedded CrispASR path.
   Expose a minimal C API equivalent to the known-good CLI mode:
   `--stream --stream-json --vad --stream-final-mode redecode
   --stream-final-on-silence-ms 1500`. The acceptance criterion is first visible
   partial before segment finalization, no duplicated text, and final text equal
   or better than current segmented final decoding on the live recordings.

2. Treat VAD as a separate backend decision.
   Keep `AudioSegmenter` as a fallback, but add a narrow adapter for CrispASR
   FireRed VAD or another neural VAD. Evaluate it on labeled clips including
   short interjections, quiet speech, pauses, and noisy microphone input. The
   metric should be missed utterances, false speech segments, endpoint delay,
   and final committed text impact, not just segment count.

3. Tune CrispSession decoding parameters before changing product defaults.
   Measure thread counts (`crispThreads` 2/4/8), max tokens, language forcing,
   and batch/chunk behavior on the same five samples plus at least ten real
   dictation recordings. The current default `crispThreads=4` is not proven
   optimal.

4. Add a reproducible ASR comparison benchmark.
   The existing `voice_latency_benchmark` now works for current CrispASR after
   applying link-time section garbage collection. Extend it to print transcript
   text and optional reference CER/WER so response speed and accuracy come from
   one command.

5. Do not remove qwen-asr-c as a fallback until CrispASR wins on the product
   metrics.
   The current evidence supports continuing CrispASR work, but it does not prove
   that the current implementation is strictly better. The release gate should
   require: no accuracy regression on real recordings, equal or lower median
   stop-time latency, working partial streaming, and VAD behavior no worse than
   the existing segmenter on short utterances.

## Reproduction Commands

Current CrispASR:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target voice_latency_benchmark -j 8
./build/tests/voice_latency_benchmark \
  --transcribe-file ~/.local/share/echoflow/recordings/live-20260621-003152.wav \
  --config ~/.config/echoflow/echoflow.conf \
  --iterations 3
```

qwen-asr-c baseline:

```bash
git worktree add /tmp/echoflow-qwen-baseline d1d97f4
cd /tmp/echoflow-qwen-baseline
git submodule update --init --recursive third_party/qwen-asr
cmake -S . -B build-qwen -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-qwen --target voice_latency_benchmark -j 8
./build-qwen/tests/voice_latency_benchmark \
  --transcribe-file ~/.local/share/echoflow/recordings/live-20260621-003152.wav \
  --config ~/.config/echoflow/echoflow.conf \
  --model-dir ~/.config/echoflow/qwen3-asr-0.6b \
  --openblas-threads 4 \
  --iterations 3
```
