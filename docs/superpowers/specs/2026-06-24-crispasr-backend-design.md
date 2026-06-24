# CrispASR Backend Design

Date: 2026-06-24

## Summary

EchoFlow's ASR engine is currently hard-wired to antirez's pure-C `qwen-asr`
runtime (`third_party/qwen-asr`) loading Qwen3-ASR safetensors. This spec adds
[CrispASR](https://github.com/CrispStrobe/CrispASR) (a whisper.cpp fork with a
native `qwen3` per-token streaming backend) plus the
[`cstr/qwen3-asr-0.6b-GGUF`](https://huggingface.co/cstr/qwen3-asr-0.6b-GGUF)
model as a **second, switchable backend**, so we can A/B whether it delivers
better latency, accuracy, or footprint than the current path.

The integration is a **subprocess + pipe** approach: `crispasr` is treated as an
external runtime dependency (like `pw-record`), spawned by a new
`CrispLiveVoicePipeline` for live mode and a new `CrispAsrEngine` for
press-to-talk. The existing qwen path stays the default and is untouched, so the
change is zero-risk to current behaviour. A new `cfg.asrEngine` flag selects the
backend.

## Problem

We want to evaluate an alternative ASR runtime without committing to a deep
build integration before we know it is better. CrispASR is attractive because:

- Its `qwen3` backend is a **native per-token streaming** backend (tokens are
  emitted as they are generated), with a purpose-built `--stream-json` event
  protocol (`partial` / `final` / `silence`) and built-in FireRed VAD.
- The GGUF model is compact (Q4_K ≈ 676 MB) and the mel filterbank is baked in,
  so the C++ runtime computes log-mel natively with no torch/onnxruntime.
- It is a single self-contained binary, easy to drop in as an external dep.

The challenge: a naive "shell out to `crispasr` per audio segment" design would
reload the GGUF on every ~1 s segment of the live pipeline, giving unusable
latency. The subprocess must therefore be **long-lived for the whole live
session** and fed PCM continuously.

## Goals

- Add CrispASR as a fully selectable backend covering **both** the default live
  mode and press-to-talk.
- Live mode exercises CrispASR's **native streaming** (its strength), not
  EchoFlow's existing energy-VAD segmenter.
- Keep the qwen backend as default; existing behaviour and tests unchanged.
- No git submodule, no new linked library, no build entanglement — `crispasr` is
  an external binary discovered on `PATH`.
- All CrispASR knowledge stays isolated inside new `Crisp*` files (mirrors the
  rule that keeps `qwen_asr.h` inside `service/AsrEngine.*`).
- Tests must not require the real `crispasr` binary, model weights, or PipeWire
  (per AGENTS.md).

## Non-Goals

- Do not replace the qwen backend as the default.
- Do not vendor CrispASR as a submodule or link its C-ABI (follow-up if it wins).
- Do not run a second always-on daemon / HTTP server.
- Do not support Qwen3-ASR-1.7B GGUF in this iteration (structurally extensible;
  0.6B only for the test).
- Do not change the Fcitx commit, UI notification, or control-socket protocol.
- Do not redesign the recorder; reuse the existing `pw-record` raw-PCM capture.

## Background: current live path

`PipeWireLiveVoicePipeline : ILiveVoicePipeline`
(`service/PipeWireLiveVoicePipeline.{h,cpp}`) is the default live path. It does
**not** use token streaming:

1. `start()` spawns `pw-record` writing raw s16le 16 kHz mono PCM to stdout
   (`buildPipeWireLiveRecordArgs`, `service/Recorder.cpp`).
2. `readerLoop()` reassembles `int16_t` samples and feeds `AudioSegmenter`
   (energy/RMS VAD), which emits `AudioSegment` blobs.
3. `SegmentAsrWorker` writes each segment to a temp WAV and calls
   `IAsrEngine::transcribe(wavPath)` on a background thread.
4. `SegmentTextAccumulator` joins stable segment text; the stable result is
   pushed via `partialTextCallback_`.
5. `finish()` stops the recorder, joins threads, returns the stable text.

Key facts used below:

- `PipeWireLiveVoicePipeline` holds a **concrete `AsrEngine&`**
  (`PipeWireLiveVoicePipeline.h`); `SegmentAsrWorker` already speaks `IAsrEngine&`.
- `VoiceSession` has two constructors: `(IRecorder&, IAsrEngine&, …)` for
  press-to-talk and `(ILiveVoicePipeline&, …)` for live; `main.cpp` selects on
  `cfg.streamTranscription`.
- Audio is 16 kHz / mono / s16le everywhere ASR sees it.

## Architecture

Three new source files, all behind the existing interfaces:

```
IAsrEngine               ILiveVoicePipeline
    ^                          ^
    |                          |
CrispAsrEngine           CrispLiveVoicePipeline
 (press-to-talk)          (live, native streaming)
                              |
                       CrispStreamAccumulator   (pure logic: JSON events -> text)
```

`main.cpp` gains a tiny factory keyed on `cfg.asrEngine`:

| `asrEngine` | `streamTranscription` | Live path                          | Press-to-talk path                    |
|-------------|-----------------------|------------------------------------|---------------------------------------|
| `qwen`      | true (default)        | `PipeWireLiveVoicePipeline`        | —                                     |
| `qwen`      | false                 | —                                  | `PipeWireRecorder` + `AsrEngine`      |
| `crisp`     | true                  | `CrispLiveVoicePipeline`           | —                                     |
| `crisp`     | false                 | —                                  | `PipeWireRecorder` + `CrispAsrEngine` |

### CrispAsrEngine (press-to-talk)

Implements `IAsrEngine::transcribe(path)`:

- `posix_spawnp` `crispasr -m <gguf> --backend <backend> -f <wav> -t <threads>
  [<extra_args>]`.
- Capture stdout, return the trimmed transcript text.
- One model load per recording is acceptable for press-to-talk.

### CrispLiveVoicePipeline (live, native streaming)

Implements `ILiveVoicePipeline`. `start()` spawns **two** children:

1. `pw-record` (raw s16le 16 kHz mono to stdout), reusing
   `buildPipeWireLiveRecordArgs`.
2. `crispasr --stream --stream-json -m <gguf> --backend qwen3 --vad
   --stream-final-on-silence-ms <N> -t <T> [<extra_args>]`.

Two worker threads:

- **Forwarder**: reads raw bytes from pw-record's stdout and writes them verbatim
  into crispasr's stdin. No resampling — crispasr consumes s16le 16 kHz mono
  directly.
- **Parser**: reads JSON lines from crispasr's stdout and feeds them to a
  `CrispStreamAccumulator`, which maps events onto `partialTextCallback_`.

### CrispStreamAccumulator (pure logic)

No I/O, fully unit-testable. Maintains `finalized_` (concatenated `final.text`
across utterances) and maps:

- `partial { utterance_id, text }` → emit `finalized_ + text` via the callback.
- `final { utterance_id, text }` → append `text` to `finalized_` (utterance
  separators handled); emit `finalized_` via the callback.
- `silence { t }` → heartbeat, no callback change.

`finalText()` returns `finalized_` (+ any trailing partial text if desired at
finish). Mirrors the stable-prefix / unstable-tail split of
`SegmentTextAccumulator`: final events are the stable prefix, the latest partial
is the unstable tail.

## Config additions

New fields on the flat `Config` struct (`service/Config.h`), parsed by
`loadDtkConf` (`service/Config.cpp`):

| INI key                         | Field                  | Default                                                       |
|---------------------------------|------------------------|---------------------------------------------------------------|
| `[basic.model] engine`          | `asrEngine`            | `"qwen"` (`"qwen"` \| `"crisp"`)                              |
| `[advanced.crisp] binary`       | `crispBinary`          | `"crispasr"` (PATH-resolved)                                  |
| `[advanced.crisp] model`        | `crispModelPath`       | `<configDir>/qwen3-asr-0.6b-q4_k.gguf`                        |
| `[advanced.crisp] backend`      | `crispBackend`         | `"qwen3"`                                                     |
| `[advanced.crisp] threads`      | `crispThreads`         | `4`                                                           |
| `[advanced.crisp] vad`          | `crispVad`             | `true`                                                        |
| `[advanced.crisp] final_on_silence_ms` | `crispFinalOnSilenceMs` | `800`                                                  |
| `[advanced.crisp] extra_args`   | `crispExtraArgs`       | `""`                                                          |

`--print-default-config` (`service/main.cpp`) emits the new keys.
`--self-test` (`service/SelfTest.cpp`): when `asrEngine == "crisp"`, add checks
that `crispasr` is on `PATH` (via `command -v`, like the `pw-record` check) and
that `crispModelPath` exists.

## Live data flow & lifecycle

`start()`:

1. Validate binary + model exist; if not, log and return failure (reported via
   `IUiNotifier`, mirroring `AsrEngine::preload` failure).
2. `posix_spawnp` pw-record and crispasr with the argv above.
3. Start forwarder + parser threads.

During speech: parser drives `partialTextCallback_` from `partial`/`final`
events.

`finish()`:

1. SIGINT pw-record (like `Recorder::stop`), wait briefly.
2. Close crispasr stdin (signals EOF → crispasr flushes trailing finals).
3. Drain crispasr stdout until EOF, bounded by `cfg.asrTimeoutSeconds`.
4. Reap both processes.
5. Return `accumulator.finalText()`.

`cancel()`:

1. SIGTERM both processes immediately.
2. Drain non-blocking, do not wait on the timeout.

## Error handling

- **Missing binary / model at `start()`**: log, surface failure through
  `IUiNotifier`; pipeline returns empty text. No crash, no throw.
- **Child crash mid-session**: detected via stdout EOF / non-zero exit; `finish()`
  returns text accumulated so far and logs the failure.
- **Malformed JSON line**: log + skip the line (parser is tolerant); never abort
  the session on one bad line.
- **`finish()` timeout**: stop draining, return whatever has accumulated, log.

## JSON parsing

The service library has no JSON dependency today. The CrispASR `--stream-json`
event schema is fixed and tiny (`type`, `utterance_id`, `text`, `t0`, `t1`). A
minimal self-contained parser lives inside `CrispStreamAccumulator.cpp` (scoped
to this schema). No new third-party JSON library is introduced, keeping all
CrispASR knowledge inside the `Crisp*` files.

## Testing

- `tests/test_config.cpp`: parse the new keys and verify defaults.
- `tests/test_crispasr_engine.cpp`: mirror `test_asr_engine.cpp` — construct
  `CrispAsrEngine` with a nonexistent binary and/or model and assert
  `transcribe()` returns empty gracefully (no throw, no crash). Requires no real
  binary or weights.
- `tests/test_crisp_stream_accumulator.cpp`: feed canned `partial`/`final`/
  `silence` JSON lines (single utterance, multi-utterance, redecode vs prefix
  fallback) and assert both the callback text sequence and `finalText()`. This is
  the core streaming-logic test and needs no binary.
- Existing `FakeAsr`/`FakeLivePipeline` tests are unchanged (interfaces are
  untouched).

## Build / runtime

- Add `CrispAsrEngine.cpp`, `CrispLiveVoicePipeline.cpp`,
  `CrispStreamAccumulator.cpp` to `service/CMakeLists.txt` source list.
- No new link dependency, no submodule. Optional `find_program(CRISPASR crispasr)`
  at configure time emits a status note only (not required to build).
- `crispasr` is an external runtime the user builds/installs separately and
  downloads the `.gguf` for (documented in the README). `echoflow_service` still
  links `qwen_asr` PUBLIC, so both backends coexist in one binary; `main.cpp`
  selects at runtime via `cfg.asrEngine`.

## Open notes

1. **Model scope**: Qwen3-ASR-0.6B GGUF only this iteration. `crispBackend` and
   `crispModelPath` make supporting 1.7B later a one-config-line change.
2. **VAD model**: default to `--vad` without an explicit `--vad-model`, relying on
   CrispASR's auto-download of the default VAD, so we bundle no extra files. If
   auto-download is undesirable offline, `crispExtraArgs` can pin a path.

## Verification

- `cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `bash -n install-user.sh uninstall-user.sh tests/spec/*.sh && sh -n run.sh`
- `./build/service/echoflow-service --print-default-config` (new keys visible)
- `./build/service/echoflow-service --self-test` (crisp checks when engine=crisp)
- Manual: set `engine = crisp` + a real `crispasr` + `.gguf`, run `./run.sh`,
  speak, observe partial/final streaming and final commit via Fcitx.
