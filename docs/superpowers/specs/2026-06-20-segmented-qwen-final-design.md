# Segmented Qwen Final Transcription Design

Date: 2026-06-20

## Summary

EchoFlow should improve long-recording latency by finalizing speech in short
segments while recording is still active. The final committed text remains
Qwen offline transcription output. Streaming partial text is used only as
temporary preview and is never committed as final text.

The selected product behavior is:

- Detect sentence-like speech segments while recording.
- Run Qwen final transcription on completed segments in the background.
- Show completed segment text as stable capsule text.
- Keep current live partial text as unstable preview when available.
- When the user stops recording, finalize the current open segment, wait for
  queued segment transcription to finish, then commit all stable Qwen final text
  once.

## Problem

The current live pipeline gives early partial feedback, but partial text is too
error-prone to commit. If EchoFlow waits for one whole-session Qwen final decode
after stop, the user sees a long `TRANSCRIBING` period on longer recordings.

The goal is to reduce stop-time waiting without sacrificing final text quality.

## Non-Goals

- Do not replace Qwen as the final transcription engine.
- Do not commit streaming partial text.
- Do not commit text while the user is still recording.
- Do not introduce a heavy external streaming engine for the first version.
- Do not redesign the UI protocol beyond reusing existing status and
  `STREAM_TEXT` updates.

## User Experience

During recording:

1. The capsule remains in recording mode.
2. Completed segment text appears as stable text.
3. Current live partial text may appear after the stable text as preview.
4. Stable text should not jump backward when live partial output changes.

When the user stops:

1. The UI switches to `TRANSCRIBING`.
2. The pipeline stops recording and flushes the current open segment.
3. EchoFlow waits for pending Qwen final segment transcriptions.
4. All stable segment text is joined and committed once through Fcitx.
5. The UI returns to `IDLE`.

If no stable segment text is available, the command returns `EMPTY` and commits
nothing.

## Architecture

The implementation stays behind the existing `ILiveVoicePipeline` interface:

```text
VoiceSession
  -> ILiveVoicePipeline::start()
  -> ILiveVoicePipeline::finish()
  -> ICommitter::commitText(final_text)
```

`VoiceSession` should not need segment-level knowledge.

Internally, `PipeWireLiveVoicePipeline` is split into these responsibilities:

```text
pw-record stdout
  -> readerLoop
  -> AudioSegmenter
  -> SegmentAsrWorker
  -> StableTextAccumulator
  -> STREAM_TEXT updates
  -> finish() final text
```

### AudioSegmenter

`AudioSegmenter` consumes 16 kHz mono PCM samples and emits complete speech
segments. The first version uses an energy-based VAD, not a model-based VAD.

Default parameters:

| Parameter | Default | Purpose |
| --- | ---: | --- |
| frame duration | 20 ms | RMS calculation window |
| end silence | 500 ms | silence needed before sealing a segment |
| min segment duration | 400 ms | avoid transcribing clicks or very short noise |
| pre padding | 200 ms | avoid clipping the start of speech |
| post padding | 200 ms | avoid clipping the end of speech |
| max segment duration | 8 s | force split if the user speaks without a pause |

The exact energy threshold should be adaptive:

1. Track a rolling noise floor while no speech is active.
2. Treat a frame as speech when RMS is clearly above the noise floor.
3. Clamp thresholds to avoid interpreting digital silence as speech.

The segmenter must expose deterministic logic for tests, independent of
PipeWire and Qwen.

### SegmentAsrWorker

`SegmentAsrWorker` accepts completed audio segments and runs Qwen offline
transcription for each segment.

Behavior:

- Segments are processed in input order.
- Each completed segment is written as a temporary WAV file.
- The worker calls `AsrEngine::transcribe(segment.wav)`.
- Empty transcription results are skipped.
- Non-empty results are appended to stable text in segment order.
- Failures are logged and do not stop later segments.

The worker should run while recording continues so that long recordings can
finish most segment transcription before the user presses stop.

### StableTextAccumulator

The accumulator owns final text produced by Qwen segment transcription.

Responsibilities:

- Preserve segment order.
- Join segment text with a conservative separator.
- Apply `stripTrailingPunctuation` only to the final commit text, matching
  current `VoiceSession` behavior.
- Provide stable text for capsule updates.

For Chinese text, the default separator should be empty string unless both
adjacent segments look like ASCII words. This avoids adding unwanted spaces in
Chinese dictation.

### Live Preview

The current Qwen live partial path can remain as unstable preview for the open
segment, but it must not be used as commit fallback.

The capsule update format can reuse:

```text
STREAM_TEXT <stable_text><preview_text>
```

The implementation may insert a lightweight visual separator in the displayed
text later, but the first version should keep the protocol unchanged.

## Finish Semantics

`PipeWireLiveVoicePipeline::finish()` should:

1. Stop `pw-record`.
2. Mark input EOF.
3. Flush the current open segment if it contains enough speech.
4. Stop accepting new segments.
5. Wait for queued segment transcription to finish.
6. Return the accumulated stable final text.

It should not wait for the existing whole-session live transcription result as
the source of final commit text.

If there is a live ASR thread for preview, `finish()` may cancel it once the
current segment has been flushed. The returned final text must come from
segment Qwen final results only.

## Time Bounds

The first version should include a stop-time guard so the service cannot wait
forever on segment ASR.

Recommended bounds:

- Per-segment ASR uses the existing Qwen timeout behavior.
- `finish()` waits for the segment worker queue to drain.
- If the queue does not drain within a bounded total timeout, return the stable
  text already completed and log the timeout.

The initial total timeout can reuse `Config::asrTimeoutSeconds` to avoid adding
a new setting before runtime behavior is understood.

## Cancellation

`cancel()` should:

- Stop `pw-record`.
- Stop accepting new audio.
- Cancel live preview transcription if running.
- Drop queued but unfinished segments.
- Join worker threads.
- Clear stable text and preview text.

No text should be committed after blur/cancel.

## Configuration

The first implementation can use internal defaults for VAD parameters. They
should be constants near the segmenter implementation and covered by tests.

Runtime config can be added later if manual tuning is needed:

- end silence duration
- minimum segment duration
- maximum segment duration
- preview enabled/disabled

Avoid exposing these settings in the UI until the defaults are validated with
real recordings.

## Testing

Add focused tests that do not require model weights, PipeWire, or Fcitx:

1. `AudioSegmenter` emits one segment after speech followed by enough silence.
2. `AudioSegmenter` ignores very short noise below `minSegmentSeconds`.
3. `AudioSegmenter` preserves pre/post padding.
4. `AudioSegmenter` force-splits very long speech at max duration.
5. Segment worker preserves output order even if mocked ASR delays differ.
6. `PipeWireLiveVoicePipeline` finish semantics are covered through a fake
   segment worker or extracted coordinator, not through real Qwen.
7. `VoiceSession` still commits once on stop and commits only final stable text.
8. Cancel clears queued/stable text and does not commit.

Manual runtime verification:

1. Start the installed service.
2. Record a short single sentence and confirm final text is no worse than the
   current Qwen path.
3. Record multiple sentences with pauses and confirm stable text appears before
   stop.
4. Stop after a long recording and confirm stop-time wait is mostly the last
   segment, not the full recording.
5. Blur/cancel during recording and confirm nothing is committed.

## Risks

### Energy VAD may split poorly

Energy-based VAD can fail in noisy rooms or on quiet speech. The mitigation is
to use conservative defaults, keep post-padding, and make the segmenter easy to
replace with a model-based VAD later.

### Segments may lose context

Qwen final transcription on shorter segments can lose cross-sentence context.
This is acceptable for the first version because it trades a small context loss
for much lower stop-time latency. Segment duration and silence thresholds should
avoid overly tiny segments.

### Background ASR may compete with live preview ASR

Running live preview and segment final transcription concurrently can increase
CPU contention. If runtime testing shows contention, prefer final segment ASR
over live preview and disable or cancel live preview earlier.

### Text joining can be awkward

Segment boundaries can produce missing punctuation or awkward joins. The first
version joins conservatively and relies on Qwen segment output. Later work can
add punctuation normalization if needed.

## Decision

Implement segmented final transcription inside `PipeWireLiveVoicePipeline` while
keeping `VoiceSession` and the external control protocol stable.

Final committed text must be built only from Qwen offline transcription of
completed segments. Streaming partial text remains preview-only.
