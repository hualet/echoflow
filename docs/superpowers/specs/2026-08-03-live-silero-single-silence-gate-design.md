# EchoFlow Live Silero Single Silence Gate Design

Date: 2026-08-03

## Goal

Make live Silero VAD hand a naturally completed utterance to ASR as soon as
Silero has confirmed the configured endpoint silence. The live adapter must
not wait for the same silence a second time.

This is a focused correction to the ongoing local Silero trial. It does not
change EchoFlow's compiled default from energy VAD and does not add semantic
sentence splitting or punctuation prediction.

## Observed Behavior

The installed service was restarted with the Silero backend, threshold `0.15`,
minimum speech `100 ms`, and minimum silence `300 ms`. Its log confirmed
`live VAD backend: silero` and a natural Silero boundary. In the user's test,
however, the boundary was queued at almost the same time as the recording was
stopped:

- detected speech interval: approximately `568-7328 ms`;
- natural segment queued after approximately `8.36 s` of recording;
- ASR result arrived another approximately `1.55 s` later;
- live audio queue peaked at only `128 ms`, ruling out queue backpressure.

The service therefore was running the requested backend, but the natural
boundary arrived too late to produce visible text before the user stopped.
This matches the reported experience that pauses did not cause text to appear.

## Root Cause

Silero endpoint silence is currently enforced in two layers:

1. `SileroVadBackend` passes `vadMinSilenceMs` to CrispASR. CrispASR keeps an
   active speech interval open until that silence has already been observed.
2. `SileroLiveSegmenter` receives the resulting interval and then requires an
   additional `endSilenceMs` worth of PCM after its end before emitting it.

With both values set to `300 ms`, one configured endpoint requires roughly
`600 ms` of silence before the adapter can emit. Analysis cadence, model
windowing, and speech padding add more timing variation. The observed delay of
roughly `700-900 ms` is consistent with this duplicated gate.

Threshold tuning cannot remove this structural delay. A threshold changes
which frames are classified as speech, while the second silence wait still
occurs after the backend has closed an interval.

## Decision

The Silero backend will be the single owner of endpoint-silence confirmation.
`SileroLiveSegmenter` will distinguish open and closed backend intervals but
will not apply another duration-based silence gate.

The interval contract is:

- an interval whose end reaches the current analyzed sample count is open and
  must not be emitted during normal streaming;
- an interval whose end is before the current analyzed sample count is closed
  by the backend and may be emitted immediately;
- `flush()` may retain an open speech interval when recording stops, preserving
  short final utterances;
- forced splitting at eight seconds remains an independent safety mechanism.

The live adapter's `endSilenceMs` setting and cached sample count will be
removed. `advanced.vad.min_silence_ms` remains supported and continues to be
passed to `SileroVadBackend`, so configuration has one clear effect and one
owner.

## Streaming Flow

For each analysis pass:

1. The live adapter sends the unfinalized rolling buffer to the reusable
   Silero backend.
2. The backend returns no interval, an open interval ending at the analyzed
   buffer length, or a closed interval ending before that length.
3. An open interval stays buffered unless the maximum segment duration forces
   a split.
4. A closed interval is emitted on that same analysis pass with the existing
   bounded pre-roll and post-roll behavior.
5. The adapter erases only the finalized prefix and retains the tail needed
   for continuity and pre-roll.

No extra timer is introduced. With a `100 ms` analysis cadence and `300 ms`
backend silence, the adapter should react on the first analysis that observes
the backend's closed interval.

## Configuration for the Next Trial

The next local trial keeps the current values so the code-path change can be
evaluated without mixing in another parameter change:

```ini
[advanced.vad.backend]
value=silero

[advanced.vad.threshold]
value=0.15

[advanced.vad.min_speech_ms]
value=100

[advanced.vad.min_silence_ms]
value=300
```

The repository default remains `energy`. Promoting Silero to the default is a
separate decision after manual experience and same-machine evaluation.

## Error Handling and Edge Cases

- Backend intervals remain normalized to the analyzed buffer bounds; empty or
  reversed intervals are ignored as they are today.
- An interval ending exactly at the analyzed buffer length is open, even if a
  fake or alternate backend labels all buffered audio as speech.
- Closed intervals preserve existing pre-roll, post-roll, absolute sample
  offsets, de-duplication, and tail retention.
- Continuous speech still force-splits at eight seconds with the configured
  overlap.
- Pure non-speech is discarded by `flush()`; open detected speech is retained.
- Reset and cancellation discard pending PCM and backend state.
- Energy VAD behavior and configuration are unchanged.

## Testing

Unit tests will use a fake backend that follows the real Silero interval
contract rather than relying only on fixed marker intervals:

1. While speech is open, the fake interval ends at the current sample count;
   the adapter must wait.
2. Once the fake backend has observed its configured silence, it returns a
   closed interval ending before the current sample count; the adapter must
   emit on that analysis without waiting for another `300 ms`.
3. An interval ending exactly at the buffer length must remain unfinished.
4. Natural emission, forced overlap, flush, reset, cadence remainder, history
   retention, and no-duplicate tests must continue to pass.

The focused test will first be added as a regression that fails with the
current duplicate gate. After the implementation, the focused test, the full
`ctest` suite, service sanity checks, and shell syntax checks must pass.

A real-model synthetic-pause probe will then verify that a closed Silero
interval is emitted after the backend's single silence requirement. This probe
uses generated audio, not a private microphone recording.

## Runtime Acceptance

After verification, the locally installed service will be rebuilt, installed,
and restarted. Before handing it back for testing, runtime evidence must show:

1. a new service PID running the newly built binary;
2. `live VAD backend: silero` with the existing `0.15/100/300` trial values;
3. a natural boundary queued after a meaningful pause and before manual stop;
4. no VAD inference, pipeline, or queue-backpressure error.

Visible text will still wait for ASR after the segment is queued. The measured
ASR time in the latest test was about `1.55 s` for a `6.76 s` segment, so this
change removes the duplicated VAD delay but does not promise instantaneous
transcription.

## Non-Goals

- changing the compiled default to Silero;
- changing the current trial threshold or silence duration;
- modifying energy VAD endpointing;
- adding semantic sentence-boundary or punctuation processing;
- changing ASR decoding, text joining, Fcitx commit, or capsule rendering;
- enabling debug-audio recording or retaining private audio;
- fixing unrelated diagnostic counters in the same change.
