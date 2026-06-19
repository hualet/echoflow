# Voice Latency Benchmark Design

## Goal

Measure why voice-to-text feels slow, optimize the highest-impact bottleneck that
is safe to change in the current architecture, and record the before/after
evidence in a local report.

## Latency Boundaries

The benchmark separates the user-visible stop-to-text path into these phases:

- `model_load_ms`: time spent loading qwen-asr before recognition can run.
- `transcribe_ms`: time spent in `qwen_transcribe()`.
- `record_stop_ms`: time spent stopping `pw-record` and validating the WAV file.
- `commit_ms`: time spent sending the recognized text to Fcitx.
- `stop_to_reply_ms`: total time spent handling the second `CTRL_DOWN` command.

The practical user complaint maps most directly to `stop_to_reply_ms`. Cold
start is tracked separately because the first recognition can include model
load, while later recognitions should not.

## Benchmark Shape

Add a native benchmark executable under `tests/benchmarks/` so it builds with the
same C++ code and dependencies as the service. It supports two modes:

- `--transcribe-file FILE [--config PATH] [--iterations N]`: real qwen-asr
  measurement using an existing WAV file and configured model directory.
- `--session-synthetic [--iterations N]`: deterministic unit-level measurement
  using fake recorder, ASR, committer, and UI implementations to validate timing
  plumbing without model weights, PipeWire, or Fcitx.

The executable prints JSON lines, one object per iteration, so results can be
saved directly in the optimization report. If no real model/audio is available,
the synthetic mode still verifies the benchmark harness and the service timing
boundaries.

## Optimization Strategy

Keep service, addon, and UI-host boundaries unchanged. The first optimization is
model preloading: load qwen-asr when the service starts instead of letting the
first stop-to-text request pay the load cost. This targets perceived latency for
the first dictation after login without changing recognition output or commit
semantics.

Expose this as an explicit `AsrEngine::preload()` method and call it from
`main()` after constructing the engine. Tests should prove that preload performs
exactly one load and that later transcription reuses the loaded context.

## Report

Write the final report to `docs/performance/voice-latency-optimization-report.md`
with:

- benchmark methodology,
- commands run,
- baseline results,
- optimization implemented,
- after results,
- remaining optimization opportunities and risks.
