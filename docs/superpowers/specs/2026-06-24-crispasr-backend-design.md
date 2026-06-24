# CrispASR Backend Evaluation & Replacement Design

Date: 2026-06-24 (revised: test-first, replace-if-better, no switchable backend)

## Summary

Evaluate [CrispASR](https://github.com/CrispStrobe/CrispASR) (whisper.cpp fork
with a native `qwen3` per-token streaming backend) + the
[`cstr/qwen3-asr-0.6b-GGUF`](https://huggingface.co/cstr/qwen3-asr-0.6b-GGUF)
model against EchoFlow's current ASR path (antirez pure-C `qwen-asr` +
safetensors). **If it is better, it replaces the current path outright** — there
is no switchable-backend config and no coexistence. This branch (`test/crispasr-
qwen3-gguf`) is the test vehicle: keep if it wins, discard if it loses.

Two phases:

1. **Phase 1 — Standalone A/B comparison.** Build `crispasr`, download the GGUF,
   run both backends on the same audio, measure latency/RTF and eyeball accuracy.
   No service integration. This answers "is it better?" fast.
2. **Phase 2 — In-service replacement (only if Phase 1 wins).** Wire CrispASR in
   as the sole ASR backend, replacing `qwen-asr-c`.

## Problem

The current path works, but CrispASR is attractive: native per-token streaming
for `qwen3`, purpose-built `--stream-json` event protocol (`partial`/`final`/
`silence`) with built-in FireRed VAD, compact GGUF (Q4_K ≈ 676 MB) with the mel
filterbank baked in (no torch/onnxruntime), and a single self-contained binary.
We want a fast, fair verdict before investing in integration.

## Non-Goals

- No switchable backend / no `asrEngine` config flag.
- No git submodule or linked C-ABI library for CrispASR (subprocess + pipe only).
- No second always-on daemon.
- No Qwen3-ASR-1.7B GGUF this iteration (0.6B only; structurally extensible).
- No changes to Fcitx commit, UI, or control-socket protocol.

## Phase 1 — Standalone comparison

### Prerequisites

- Clone + build CrispASR: `~/projects/CrispASR`, `cmake -B build
  -DCMAKE_BUILD_TYPE=Release && cmake --build build -j16 --target crispasr`.
- Download `qwen3-asr-0.6b-q4_k.gguf` to `~/.config/echoflow/crisp/`.

### Method

Run both backends on the same clips and compare. Audio set:

- Repo samples: `third_party/qwen-asr/samples/{jfk.wav,test_speech.wav}`.
- A short Chinese clip (the primary EchoFlow use case) — record one with the
  existing `pw-record` path or grab a sample.
- Optionally a longer clip from `samples/night_of_the_living_dead_1968/`.

CrispASR invocation (batch, file mode):

```
crispasr -m <gguf> --backend qwen3 -f <wav> -t 8
```

Current path: the existing benchmark harness
(`tests/benchmarks/voice_latency_benchmark.cpp`) and the numbers in
`docs/performance/voice-latency-optimization-report.md` give the qwen-asr-c
baseline; `./build/service/echoflow-service --transcribe-file <wav>` gives an
end-to-end number.

### Metrics

- **Cold/warm total latency** and **RTF** (realtime factor) per clip.
- **First-token latency** for streaming (CrispASR `--stream` partial time vs the
  segmented pipeline's first stable segment).
- **Accuracy**: side-by-side transcript text; flag substitutions/drops.
- **Memory / model size** (676 MB GGUF vs safetensors).

### Verdict gate

Proceed to Phase 2 only if CrispASR is at least comparable on accuracy and better
(or clearly competitive) on live first-token latency. Otherwise discard the
branch.

## Phase 2 — In-service replacement (conditional)

If Phase 1 wins, replace `qwen-asr-c` with CrispASR as the sole backend via a
**subprocess + pipe** integration. `crispasr` stays an external PATH binary
(like `pw-record`); all CrispASR knowledge isolated in new `Crisp*` files.

### Live mode — `CrispLiveVoicePipeline : ILiveVoicePipeline` (replaces `PipeWireLiveVoicePipeline`)

`start()` spawns two children: `pw-record` (raw s16le 16 kHz mono to stdout,
reusing `buildPipeWireLiveRecordArgs`) and a single long-lived
`crispasr --stream --stream-json -m <gguf> --backend qwen3 --vad
--stream-final-on-silence-ms <N> -t <T>`. A forwarder thread pipes pw-record's
raw bytes verbatim into crispasr's stdin (no resampling). A parser thread reads
JSON lines into a pure-logic `CrispStreamAccumulator`:

- `partial` → callback gets `finalized_ + partial.text`.
- `final` → append to `finalized_`; callback gets `finalized_`.
- `silence` → heartbeat.

`finish()` SIGINTs pw-record, closes crispasr stdin, drains trailing finals
(bounded by `cfg.asrTimeoutSeconds`), reaps both, returns accumulated text.
`cancel()` SIGTERMs both without blocking.

A persistent process is mandatory — a naive per-segment subprocess would reload
the GGUF each ~1 s segment (unusable latency).

### Press-to-talk — `CrispAsrEngine : IAsrEngine` (replaces `AsrEngine`)

`transcribe(path)` = one-shot `crispasr -m <gguf> --backend qwen3 -f <wav>`.
Also serves `--transcribe-file`.

### Config (Phase 2)

Add to `Config` + `loadDtkConf` (no engine switch):

| INI key                                 | Field                    | Default                                       |
|-----------------------------------------|--------------------------|-----------------------------------------------|
| `[advanced.crisp] binary`               | `crispBinary`            | `"crispasr"`                                  |
| `[advanced.crisp] model`                | `crispModelPath`         | `<configDir>/crisp/qwen3-asr-0.6b-q4_k.gguf`  |
| `[advanced.crisp] backend`              | `crispBackend`           | `"qwen3"`                                     |
| `[advanced.crisp] threads`              | `crispThreads`           | `4`                                           |
| `[advanced.crisp] vad`                  | `crispVad`               | `true`                                        |
| `[advanced.crisp] final_on_silence_ms`  | `crispFinalOnSilenceMs`  | `800`                                         |
| `[advanced.crisp] extra_args`           | `crispExtraArgs`         | `""`                                          |

`main.cpp` constructs `CrispLiveVoicePipeline` (live) or `PipeWireRecorder` +
`CrispAsrEngine` (press-to-talk) directly. The old `AsrEngine` /
`PipeWireLiveVoicePipeline` / `qwen-asr-runtime` / `third_party/qwen-asr` are
removed once the replacement is verified.

### Error handling

Missing binary/model at start → log + report via `IUiNotifier`, return empty
(mirrors `AsrEngine::preload` failure). Child crash mid-session → return text
accumulated so far + log. Malformed JSON line → log + skip, never abort.
`finish()` timeout → stop draining, return accumulated text, log.

### JSON parsing

The service lib has no JSON dep. A minimal self-contained parser for the fixed
`--stream-json` schema (`type`, `utterance_id`, `text`, `t0`, `t1`) lives inside
`CrispStreamAccumulator.cpp`. No new third-party JSON library.

### Testing (Phase 2)

- `tests/test_config.cpp`: parse new keys + defaults.
- `tests/test_crispasr_engine.cpp`: nonexistent binary/model → `transcribe()`
  returns empty gracefully (mirrors `test_asr_engine.cpp`; no real binary).
- `tests/test_crisp_stream_accumulator.cpp`: canned JSON lines → assert callback
  text + final text (no binary needed).
- Existing `FakeAsr`/`FakeLivePipeline` tests unchanged (interfaces untouched).

### Build (Phase 2)

Add `CrispAsrEngine.cpp`, `CrispLiveVoicePipeline.cpp`,
`CrispStreamAccumulator.cpp` to `service/CMakeLists.txt`. No new link dep;
optional `find_program(CRISPASR)` at configure time for a status note.

## Verification

- Phase 1: the comparison table (latency/RTF/accuracy) — the verdict gate.
- Phase 2: `cmake --build build && ctest --test-dir build --output-on-failure`;
  `./build/service/echoflow-service --print-default-config`;
  `./build/service/echoflow-service --self-test`; manual live run via `./run.sh`.
