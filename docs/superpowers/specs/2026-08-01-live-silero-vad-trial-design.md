# EchoFlow Live Silero VAD Trial Design

Date: 2026-08-01

## Goal

Enable Silero VAD in EchoFlow's production live-recording path on the current
machine so its sentence segmentation can be evaluated with normal dictation.
The trial must be explicitly selectable and easy to roll back. It does not
change the repository default from the existing energy VAD.

## Current Behavior and Evidence

`Config` already accepts `advanced.vad.backend` and `advanced.vad.model`, and
the repository contains a native Silero 6.2 model plus an offline
`SileroVadBackend`. The production `CrispLiveVoicePipeline`, however, always
constructs `AudioSegmenter`; the VAD configuration does not affect live
recording.

The current energy segmenter ends a segment after 500 ms of frames classified
as silence or force-splits it after eight seconds. Runtime logs from 2026-08-01
showed recordings being split at exactly 8,000 ms while the observed natural
pauses never reached the energy endpoint. This explains why dictation feels as
if it is not being split into sentences.

The existing `SileroVadBackend::detect()` is an offline whole-buffer helper.
Calling it unchanged for each live PCM block would repeatedly load the model
and is not acceptable for a real-time path.

## Scope

This trial will:

- add a live Silero segmenter with the same externally observable lifecycle as
  the energy segmenter;
- select the live segmenter from `advanced.vad.backend`;
- keep the Silero model loaded for the duration of one recording session;
- expose the small set of Silero parameters needed for local tuning;
- preserve the existing eight-second safety split and ordered ASR queue;
- report the selected backend and emitted boundaries in service logs;
- install the repository's Silero 6.2 test model into the user's EchoFlow
  configuration directory and enable it only in the local configuration.

This trial will not:

- change the compiled default away from `energy`;
- remove or retune `AudioSegmenter`;
- download a model from the service;
- change ASR decoding, text joining, Fcitx commit behavior, or UI settings;
- claim benchmark-quality miss-rate or false-activation results from an
  unlabeled manual trial.

## Architecture

### Live Segmenter Boundary

Introduce a narrow live-segmentation interface with `append`, `flush`,
`reset`, `name`, and diagnostics operations. `AudioSegmenter` and the new
Silero implementation conform to this boundary and both emit the existing
`AudioSegment` type with absolute sample offsets.

`LiveSegmentCoordinator` owns the selected live segmenter rather than a
concrete `AudioSegmenter`. Its ASR queue, result ordering, cancellation, and
metrics remain unchanged. Existing callers and unit tests may use an energy
convenience constructor so this refactor does not alter their behavior.

`CrispLiveVoicePipeline` creates the implementation selected by `Config`:

```text
advanced.vad.backend=energy -> AudioSegmenter
advanced.vad.backend=silero -> SileroLiveSegmenter
anything else               -> startup error
```

Selection is explicit. A requested Silero backend never silently falls back to
energy, because that would make a manual trial appear successful while testing
the wrong algorithm.

### Silero Model Lifetime

The live Silero implementation owns one CrispASR VAD context. It loads the
model when the recording pipeline starts, reuses the context for every
analysis pass, and frees it when the segmenter is destroyed. Model
initialization and inference remain inside the VAD implementation; no
CrispASR VAD types leak into `LiveSegmentCoordinator`.

The existing offline backend will use the same reusable model wrapper so the
benchmark and live path share threshold and interval semantics. Tests inject a
fake detector and do not require the model file.

### Rolling Endpoint Algorithm

The live segmenter preserves incoming PCM and absolute sample positions. At a
fixed analysis cadence it runs Silero over the unfinalized rolling buffer.
Only a detected speech interval followed by the configured amount of observed
silence is final. This prevents an interval touching the newest PCM from being
emitted while the speaker is still talking.

For each final interval, the segmenter:

1. includes bounded pre-roll before detected speech;
2. includes bounded post-roll after the detected speech end;
3. emits an `AudioSegment` with absolute begin and end sample offsets;
4. retains only the unfinalized tail needed for the next pre-roll;
5. never emits the same samples twice except for the deliberate overlap at a
   forced maximum-duration split.

If continuous speech has no natural endpoint by eight seconds, the existing
eight-second maximum and 500 ms overlap apply. `flush()` emits detected open
speech at recording stop, including a short utterance that has not yet
accumulated endpoint silence. Pure non-speech is discarded. `reset()` and
`cancel()` discard buffered PCM and VAD state.

The initial trial values are:

- threshold: `0.10`;
- minimum speech: `100 ms`;
- endpoint silence: `300 ms`;
- analysis cadence: `100 ms`;
- pre-roll: `200 ms`;
- post-roll: `200 ms`;
- maximum segment: `8,000 ms`;
- forced overlap: `500 ms`.

The permissive initial threshold prioritizes avoiding missing speech during
the first trial. It is not a proposed production default. Threshold, minimum
speech, and endpoint silence are configurable so subsequent trials can tune
false activation, short utterance retention, and sentence latency without
rebuilding.

## Configuration and Local Activation

Add these optional entries under the existing advanced VAD section:

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

Defaults for the new numeric fields match the trial values above but have no
effect while the backend is `energy`. Invalid thresholds or non-positive
durations are rejected during configuration loading rather than clamped
silently.

The activation step copies the already vendored test model to the local model
path, updates the user's configuration, installs the newly built binaries with
the repository's normal user-install workflow, and restarts the user service.
The service log must show `live VAD backend: silero` and Silero boundary
diagnostics before the trial is handed to the user.

The local configuration edit and copied model are runtime trial artifacts and
are not committed. Rolling back requires setting the backend to `energy` and
restarting the service; the copied model may remain harmlessly installed.

## Failure Handling

Selecting Silero fails pipeline startup with an actionable message when:

- the model path is empty or is not a regular file;
- the model cannot be initialized;
- an inference call fails;
- the backend name or numeric VAD configuration is invalid.

No partial transcript is committed after a live VAD error. The error follows
the existing live-pipeline failure path. Energy mode does not require or
inspect a Silero model.

## Diagnostics and Privacy

Normal logs record the selected backend, analysis duration, detected speech
intervals, emitted segment boundaries, and whether a boundary was natural,
forced, or flushed. They do not record PCM or recognized text.

The existing opt-in `save_live_debug_audio` behavior is unchanged. This trial
does not enable it automatically and does not create or commit recordings.

## Testing

Unit tests use a fake interval detector and deterministic PCM to verify:

- a natural pause emits a segment before the eight-second maximum;
- an unfinished speech interval is not emitted prematurely;
- a short utterance is retained by `flush()`;
- non-speech is discarded by `flush()`;
- pre-roll and post-roll are bounded at stream edges;
- continuous speech force-splits with the existing overlap;
- repeated analysis does not duplicate naturally finalized audio;
- reset and cancellation discard pending state;
- backend selection chooses energy or Silero and rejects unknown values;
- missing and invalid Silero models fail explicitly.

Existing `AudioSegmenter`, `LiveSegmentCoordinator`, live-pipeline,
configuration, self-test, and replay-benchmark tests must remain green. The
full `ctest` and shell syntax checks run before local installation.

## Trial Acceptance

The implementation is ready for manual use when:

1. automated tests pass without requiring microphone access or model weights;
2. the installed service log proves that the live backend is Silero;
3. a sentence followed by a natural pause produces a segment before eight
   seconds;
4. stopping after a short utterance still produces text;
5. returning to `energy` requires only a configuration edit and service
   restart.

Promotion of Silero to the repository default is a separate decision. It
requires same-machine replay or labeled evaluation covering sentence
boundaries, missed speech, false activation, CER, first stable-text latency,
and stop latency. Manual preference alone is useful for this trial but is not
enough to change the default.
