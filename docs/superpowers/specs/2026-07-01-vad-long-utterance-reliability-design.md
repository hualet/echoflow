# EchoFlow VAD and Long-Utterance Reliability Design

Date: 2026-07-01

## Goal

Improve EchoFlow's production voice-input path so it is measurably better than
the current implementation in three areas:

- voice activity detection under quiet speech, background noise, and pauses;
- end-to-end latency while recording and when committing final text;
- transcript completeness for long, continuous dictation.

The work is complete only when a reproducible report compares the new and
current paths on the same audio and shows no regression in any of these areas.

## Current Failure Modes

`CrispLiveVoicePipeline` currently reads PipeWire PCM and runs synchronous ASR
on the same thread. While ASR is running, the pipe is not drained. A slow decode
can therefore apply backpressure to `pw-record` and lose live audio.

`AudioSegmenter` is an RMS threshold detector. It cannot distinguish speech
from noise with semantic confidence and is sensitive to microphone gain, quiet
speech, and short utterances. It also force-splits continuous speech at eight
seconds without overlap or cross-segment context.

The existing `save_live_debug_audio` setting is parsed but is not connected to
the live capture path. That prevents exact replay of production failures.

## Scope

This change will:

1. make PCM capture independent from ASR latency;
2. record enough diagnostics to reproduce segmentation and loss;
3. introduce a neural VAD behind a narrow backend interface;
4. preserve the current energy segmenter as a fallback;
5. improve forced splitting for long continuous speech;
6. add deterministic and real-audio benchmarks;
7. publish a comparison report with positive and negative results.

This change will not initially replace final segment decoding with unstable
partial text, add cloud services, or change the Fcitx commit protocol.

## Architecture

The live path will have three independent responsibilities:

```text
pw-record -> CaptureWorker -> PCM queue -> SegmentWorker -> segment queue
                                                        -> AsrWorker -> ordered text
```

### CaptureWorker

The capture worker continuously drains the PipeWire pipe and never calls ASR.
It assigns monotonically increasing sample offsets and pushes fixed-size PCM
blocks to a bounded queue. A 16 kHz mono S16 stream consumes only 32 KB/s, so
the queue can absorb several minutes without material memory pressure.

Queue pressure must not silently discard audio. If a safety limit is reached,
the pipeline logs the sample range and returns a visible failure instead of
committing a known-incomplete transcript.

When debug audio is enabled, the same blocks are written to one WAV file. File
writing must not block capture; it uses the downstream worker or a buffered
writer.

### SegmentWorker and VAD Backend

Segmentation consumes blocks in sample order. A small `IVadBackend` interface
will expose speech probabilities or speech decisions without exposing a
specific model API to the pipeline.

The preferred backend is the neural VAD already supported by the vendored
CrispASR runtime, provided its model can be packaged and loaded offline. If its
API or model lifecycle proves unsuitable during the implementation spike,
Silero VAD may implement the same interface. `AudioSegmenter` remains the
fallback when the neural model is unavailable.

Endpointing applies hysteresis rather than one threshold:

- speech requires consecutive positive frames to start;
- a longer run of negative frames ends speech;
- pre-roll and post-roll protect initial and trailing phonemes;
- short interjections are retained when neural confidence supports them;
- natural silence is preferred over maximum-duration splitting.

Initial timing values are experiment inputs, not acceptance criteria. The
evaluation selects them from labeled recordings rather than intuition.

### Long-Utterance Splitting

The current eight-second hard boundary will be removed. Natural VAD endpoints
produce ordinary segments. Continuous speech receives a higher maximum window,
initially 15 seconds, and the splitter searches a nearby low-speech-confidence
region before forcing a boundary.

If no safe boundary exists, adjacent segments retain a short overlap. Text
joining uses token-aware suffix/prefix matching with conservative limits. It
must prefer a duplicated phrase over deleting non-identical text. The report
will separately measure boundary omissions and duplications.

### AsrWorker

A single ASR worker owns `CrispSession` and processes segments in order. This
avoids assuming that the session is thread-safe and guarantees deterministic
result order. Capture and segmentation continue while decoding runs.

`finish()` stops capture, drains remaining PCM, flushes the open segment, waits
for queued ASR work, then joins final text. `cancel()` stops all workers, clears
queued audio and text, and commits nothing. Worker errors are retained and
reported; later segments are not silently presented as a complete result.

## Configuration and Packaging

User-facing configuration should remain small. Production defaults select the
neural backend with automatic fallback. Experimental thresholds belong in an
advanced section and are added only when they are necessary for evaluation or
support.

The VAD model must be available offline, included in the model catalog or
packaged with an explicit download entry, checksum, and self-test. The service
must not download it.

`save_live_debug_audio=true` produces a WAV plus a sidecar containing:

- capture start and end sample offsets;
- VAD probability/decision ranges;
- emitted segment ranges and forced-boundary reasons;
- queue high-water marks and ASR duration per segment;
- final joined text.

Debug recording remains opt-in because it contains private speech.

## Evaluation

### Baseline

Before changing behavior, preserve the current binary or build a baseline
worktree. Run both implementations on identical WAV inputs. Live-capture tests
also verify that the captured WAV duration and sample count match wall time
while ASR is deliberately slowed.

### Dataset

The dataset includes existing repository/local recordings plus newly labeled
cases covering:

- quiet and normal-volume Mandarin speech;
- short interjections;
- long continuous speech of at least 30 and 60 seconds;
- natural pauses between clauses;
- constant fan/room noise and transient keyboard or click noise;
- speech beginning immediately after capture starts;
- speech ending immediately before the stop command.

Private recordings remain local and are referenced by a manifest that records
duration, conditions, and expected text without committing the audio.

### Metrics

The report will compare:

- **capture completeness:** expected versus captured samples and discontinuity
  count;
- **VAD misses:** labeled speech duration not included in any segment;
- **false activation:** labeled non-speech duration included in segments;
- **endpoint delay:** end of labeled speech to segment emission;
- **recognition completeness:** Mandarin CER and explicit boundary omissions;
- **duplication:** repeated characters or words introduced at joins;
- **first stable text latency:** speech start to first finalized segment text;
- **stop latency:** stop command to final text;
- **real-time factor and queue high-water mark.**

### Acceptance Criteria

Compared with the current path on the same dataset, the candidate must:

1. capture all PCM samples in the slowed-ASR stress test with no silent drops;
2. reduce total missed-speech duration and not increase false activation by
   more than one percentage point;
3. reduce aggregate CER and produce no new systematic boundary omission;
4. improve or preserve median first-stable-text and stop latency;
5. successfully commit the complete ordered result for 30- and 60-second
   dictation;
6. pass the full unit/spec suite and installed-runtime self-test.

If neural VAD fails these gates, the report keeps the result and production
continues using the best measured fallback rather than promoting it by design.

## Test Strategy

Unit tests cover queue ordering and pressure, capture shutdown, cancellation,
worker errors, VAD hysteresis, pre/post-roll, natural and forced boundaries,
overlap joining, and ordered result assembly. These tests use fake capture,
VAD, and ASR implementations and require no model or PipeWire daemon.

Integration tests stream deterministic PCM through the coordinator with a slow
fake ASR and assert exact sample preservation. Benchmark tools replay WAV files
through both VAD backends and emit machine-readable metrics. Manual validation
uses the installed service, real microphone, saved debug audio, and Fcitx text
commit.

## Delivery Order

1. Add baseline diagnostics and evaluation fixtures.
2. Separate capture, segmentation, and ASR work without changing the VAD.
3. Verify sample completeness and publish the intermediate comparison.
4. Add the neural VAD backend and evaluate it against the energy fallback.
5. Tune endpointing and long-utterance boundaries from measured results.
6. Run unit, integration, benchmark, and installed-runtime validation.
7. Write the final report, including regressions and rejected parameter sets.

Native CrispASR partial streaming remains a later experiment. It may replace
the segmented path only after it independently meets the same final-text and
latency gates.
