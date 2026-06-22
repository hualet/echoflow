# SenseVoice Integration Research

Date: 2026-06-22

## Summary

SenseVoice is a good fit for an EchoFlow low-latency experiment, but it should
be integrated as a selectable backend before it replaces Qwen as the only ASR
engine.

The benchmarked SenseVoiceSmall GGUF path is much faster than the current Qwen
final decode path on local EchoFlow recordings. The observed quality gap is not
zero, though: SenseVoice produced several visible word substitutions and
under-split some longer dictation samples. That makes it attractive for an
experience branch and optional low-latency mode, but not enough evidence for an
immediate unconditional production switch.

## Recommendation

Use SenseVoice for a real integration branch with these constraints:

- Keep Qwen support available as a fallback engine.
- Add SenseVoice as the default candidate in the experiment branch, not as an
  irreversible architecture removal.
- Download both `sensevoice-small-q8.gguf` and `fsmn-vad.gguf` as one logical
  model choice.
- Treat the SenseVoice runtime binary as a packaged runtime dependency, because
  the upstream generic Linux binary failed on this host with an illegal
  instruction and a local CPU-native build was required.
- Preserve EchoFlow's current segmented/live pipeline, then swap only the final
  segment transcription backend first.

## Evidence

The detailed benchmark is recorded in:

```text
docs/performance/sensevoice-cpp-benchmark.md
```

Key numbers from that report:

| Metric | Qwen | SenseVoice |
| --- | ---: | ---: |
| Total EchoFlow audio duration | 113.992 s | 113.992 s |
| Average final-ASR latency | 4205.1 ms | 392.5 ms compute / 592.0 ms wall |
| Approximate real-time factor | 0.295 | 0.028 compute / 0.042 wall |

The latency gain is large enough to change the perceived stop-time delay. Even
when measured as a separate CLI process including model loading, SenseVoice was
still substantially faster than Qwen's warmed final decode in these samples.

## Accuracy Risk

SenseVoice was not consistently worse, but the observed mistakes are
user-visible in a voice input method:

| Intended or Qwen text | SenseVoice text |
| --- | --- |
| `这次` | `电池` |
| `上屏` | `商品` |
| `不太靠谱` | `不太够` |
| `分段` / `采分` context | `裁分` or related confusion |

SenseVoice also returned no speech for one short `嗯` sample because the VAD
reported zero segments. EchoFlow should handle that case explicitly before a
production rollout, either by falling back to Qwen for short recordings or by
retrying without VAD when the first SenseVoice result is empty.

## Streaming Fit

SenseVoice C++ in the evaluated GGUF form is best treated as a very fast final
segment recognizer rather than a true incremental streaming recognizer. This
still matches EchoFlow's current direction because the service already records
and processes speech segments during a session.

The practical integration path is:

1. Keep PipeWire capture and EchoFlow's live segmentation unchanged.
2. Feed each finalized segment to SenseVoice for fast text generation.
3. Accumulate segment text with the existing text accumulator.
4. Commit the final accumulated result through the existing Fcitx path.

This approach improves perceived latency without depending on concatenated
partial ASR output, which earlier streaming research showed is not reliable
enough for final committed text.

## Model Download Requirements

The model catalog needs one selectable SenseVoice item with files from two
Hugging Face repositories:

| File | Repository |
| --- | --- |
| `sensevoice-small-q8.gguf` | `FunAudioLLM/SenseVoiceSmall-GGUF` |
| `fsmn-vad.gguf` | `FunAudioLLM/fsmn-vad-GGUF` |

Both files should be stored under the same resolved EchoFlow model directory so
the ASR backend can load them together. The downloader therefore needs
per-file repository metadata instead of assuming every file for one model comes
from one repository.

## Runtime Packaging Requirements

The integration should provide a deterministic way to build or install
`llama-funasr-sensevoice`.

The current experiment uses:

- SenseVoice source: `FunAudioLLM/SenseVoice`
- GGUF runtime: SenseVoice's `runtime/llama.cpp`
- llama.cpp source: pinned by commit
- CMake options: CPU-native build, curl disabled, external llama.cpp source

The runtime setup should install the binary under the same prefix as the rest
of EchoFlow, usually `~/.local/bin` for local installs. Self-test should fail
with an actionable message when the selected model is SenseVoice but the
runtime binary is missing.

## Open Decisions Before Replacing Qwen

- Whether SenseVoice should be the shipped default or only an optional low
  latency backend.
- Whether empty VAD results should retry without VAD, retry with Qwen, or
  return empty text.
- Whether final punctuation and sentence splitting need an EchoFlow-side
  postprocessor.
- Whether the package should build SenseVoice runtime at install time, ship a
  prebuilt binary, or build it as part of Debian packaging.
- Whether a larger labeled Chinese dictation set changes the accuracy tradeoff.

## Proposed Next Step

Use the `sensevoice` branch for hands-on testing. The branch should prove the
full loop before any mainline decision:

1. Model catalog lists SenseVoice first and downloads both required GGUF files.
2. Settings UI exposes a SenseVoice download entry.
3. Self-test validates model files and runtime binary.
4. The service transcribes files and live segments through SenseVoice.
5. Local install restarts the service so the user can dictate into Fcitx.

If the branch feels faster but quality issues are noticeable during daily
dictation, keep SenseVoice as an optional backend and continue using Qwen as
the default final-text engine.
