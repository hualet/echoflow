# Live Stream ASR Default Design

## Goal

Reduce the delay after the user stops recording by running ASR while recording
is still in progress. The first implementation keeps the current tap-to-talk
interaction: press right Ctrl to start, press right Ctrl again to stop, then
commit the final recognized text. EchoFlow will not show partial text and will
not commit partial text while recording.

## Problem

EchoFlow currently records a complete WAV file with `pw-record`, stops the
recorder, then calls `AsrEngine::transcribe(audioPath)`. The user waits for the
full recording shutdown and the full ASR pass after the second right Ctrl.

Calling `qwen_transcribe_stream(ctx, samples, nSamples)` on a complete in-memory
WAV does not solve this. In the current qwen-asr implementation, file-backed
streaming without a token callback skips the chunk loop and performs a direct
final refinement. True latency reduction needs audio to reach qwen-asr before
recording stops.

## User Experience

- First right Ctrl starts recording and immediately starts a background
  streaming ASR session.
- The tooltip remains in the current recording state. No partial recognized
  text is shown in this phase.
- Second right Ctrl stops microphone capture, waits for the streaming ASR
  session to finish its final text, strips trailing punctuation if configured,
  and commits the final text.
- Blur while recording cancels the streaming session and discards any partial or
  final text.
- Empty recognition returns to idle without committing text, matching the
  current behavior.

## Architecture

Introduce a live voice pipeline boundary between `VoiceSession` and the concrete
recording/ASR implementation.

```text
VoiceSession
  -> ILiveVoicePipeline::start()
       -> PipeWire raw PCM subprocess
       -> PCM reader thread
       -> qwen live audio buffer
       -> ASR worker thread: qwen_transcribe_stream_live()
  -> ILiveVoicePipeline::finish()
       -> stop recorder input
       -> mark live buffer EOF
       -> join ASR worker
       -> return final text
  -> Committer::commitText(final text)
```

`VoiceSession` owns only the state-machine behavior. The live pipeline owns
process, pipe, thread, qwen live buffer, and ASR lifetime details.

## Components

### `ILiveVoicePipeline`

Add a focused interface for the tap-to-talk live path:

```cpp
class ILiveVoicePipeline {
public:
    virtual ~ILiveVoicePipeline() = default;
    virtual void start() = 0;
    virtual std::string finish() = 0;
    virtual void cancel() = 0;
};
```

`finish()` returns the final ASR text or an empty string. It may throw on
unexpected runtime failures; `VoiceSession` catches failures and treats them as
empty recognition, as it does today for `IAsrEngine::transcribe()`.

### `PipeWireLiveVoicePipeline`

Create the production implementation in `service/`. It will:

- Start `pw-record` with raw stdout output instead of a WAV file. The requested
  capture format is mono `s16le` at 16 kHz so the stream can be appended to
  qwen-asr without file parsing or resampling.
- Read stdout from a service-owned pipe in a reader thread.
- Convert `s16le` PCM frames to float samples in `[-1.0, 1.0]`.
- Append samples into a service-owned `qwen_live_audio_t` compatible buffer
  guarded by its mutex and condition variable.
- Start an ASR worker thread that calls `qwen_transcribe_stream_live(ctx, live)`.
- On `finish()`, close the recorder stream, terminate/wait for the `pw-record`
  child, mark EOF on the live buffer, wake qwen-asr, join the ASR worker, and
  return the final string.
- On `cancel()`, stop the child, mark EOF, join all threads, and discard the
  final string.

Do not use `qwen_live_audio_start_stdin()`: it reads from process stdin and
owns its own reader thread, while EchoFlow needs to manage a `pw-record`
subprocess and pipe. The service should initialize and destroy the
`qwen_live_audio_t` fields it uses directly.

### `AsrEngine`

Keep `AsrEngine::transcribe(path)` for `--transcribe-file`, tests, and fallback
tooling. Add a live-session entry point used by `PipeWireLiveVoicePipeline`, for
example `AsrEngine::transcribeLive(qwen_live_audio_t*)` or a small
`QwenLiveAsrSession` helper that shares the existing model loading, language,
prompt, silence-skip, and OpenBLAS setup logic.

The live path must install no UI-facing token callback in this phase. The final
text returned by `qwen_transcribe_stream_live()` is the only output consumed by
EchoFlow.

### `VoiceSession`

Change the production session wiring to use `ILiveVoicePipeline` for interactive
right-Ctrl recording. The state machine remains the same:

- Idle + `CTRL_DOWN`: `pipeline.start()`, send `RECORDING`.
- Recording + `CTRL_DOWN`: send `TRANSCRIBING`, call `pipeline.finish()`, then
  commit final text or return `EMPTY`.
- Recording + `BLUR`: `pipeline.cancel()`, send `HIDE_TOOLTIP`, return idle.

The existing file-based `IRecorder` + `IAsrEngine` path can remain available for
benchmarks and targeted tests until the live path has equivalent coverage.

## Error Handling

- If `pw-record` fails to spawn, `start()` throws; `VoiceSession` returns to idle
  and sends `IDLE`.
- If the ASR worker fails to load the model or returns no text, `finish()`
  returns an empty string.
- If stopping the recorder times out, terminate the child process, mark EOF, and
  still join the ASR worker before returning.
- `cancel()` is idempotent and must be safe from `Recording` blur and object
  destruction.
- No text is committed from a canceled session.

## Testing

Use TDD for the implementation.

### Unit Tests

- `VoiceSession` starts the live pipeline and sends `RECORDING`.
- Second right Ctrl calls `finish()`, commits the returned final text, sends
  `TRANSCRIBING`, then sends `IDLE`.
- Empty final text returns `EMPTY` and does not commit.
- A `finish()` exception returns to idle and does not commit.
- Blur while recording calls `cancel()` and does not call `finish()` or commit.
- `cancel()` is idempotent in the fake live pipeline.
- PCM conversion maps little-endian `s16le` samples to expected floats.

### Integration/Manual Verification

- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `./build/service/echoflow-service --self-test`
- Manual runtime check after install: restart `echoflow.service`,
  `echoflow-ui.service`, and Fcitx if addon behavior was touched, then record a
  short utterance and verify the post-stop wait is shorter than the old WAV path.

## Out of Scope

- Showing partial recognized text in the tooltip.
- Committing partial text while recording.
- Changing the Fcitx key-capture behavior.
- Rewriting the settings UI.
- Removing `AsrEngine::transcribe(path)` or `--transcribe-file`.
- Benchmark result claims without rerunning on the target machine.

## Migration Notes

There is no user configuration migration. The existing `streamTranscription`
benchmark-only flag can be removed separately, but removing it is not the live
streaming feature. Any benchmark documentation that mentions `--stream` must be
updated in the same change that removes the option.
