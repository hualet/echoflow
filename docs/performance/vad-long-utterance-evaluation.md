# EchoFlow VAD and Long-Utterance Evaluation

Date: 2026-07-01

Status: in progress. This report records the authoritative baseline and is
updated after each candidate stage. It does not claim final acceptance yet.

## Objective

Improve the production voice-input path over commit `e43f540` in three
independent dimensions:

- fewer missed speech regions and false activations;
- lower first-stable-text and stop latency;
- fewer omitted characters in long and continuous dictation.

## Environment

- Host: `hualet-box`, x86-64, deepin 25
- Build: GCC 12.3, `RelWithDebInfo`
- ASR: Qwen3-ASR 0.6B Q4_K through CrispASR, six threads
- Input: 16 kHz mono S16 WAV
- Config: `~/.config/echoflow/echoflow.conf`

Private WAV files stay under `~/.local/share/echoflow/recordings/`. The local
manifest used for this run is `/tmp/echoflow-vad-baseline-manifest.json`; raw
JSONL is `/tmp/echoflow-vad-energy-baseline.jsonl`. Neither contains committed
audio.

## Baseline Architecture Finding

At `e43f540`, `CrispLiveVoicePipeline::readerLoop()` performs
`CrispSession::transcribe()` synchronously after each completed segment. During
that decode it does not read from the `pw-record` pipe. Existing real-file
measurements show an ASR real-time factor around 0.43-0.53, so an eight-second
segment can stop pipe draining for several seconds. This is a direct mechanism
for live-capture backpressure and omitted audio.

The candidate now queues segments to one ASR worker. A deterministic stress test
feeds 40 seconds of continuous PCM while every one-second segment decode sleeps
20 ms. It verifies:

- input samples: 640,000;
- samples represented by queued segments: 640,000;
- segments queued/completed: 40/40;
- ASR queue high-water mark: greater than one;
- reported audio drops: false.

This proves the candidate capture/segmentation path no longer waits for segment
ASR. Installed PipeWire validation remains required before final acceptance.

## Energy VAD and Transcript Baseline

Command:

```bash
./build/tests/vad_replay_benchmark \
  --manifest /tmp/echoflow-vad-baseline-manifest.json \
  --backend energy \
  --config ~/.config/echoflow/echoflow.conf
```

The four existing recordings have transcript references but not independently
annotated speech intervals. Therefore this table can support segmentation,
latency, and CER conclusions, but not a VAD miss-rate or false-activation-rate
claim.

| Recording | Duration | Segments | Decode | CER | Important result |
| --- | ---: | ---: | ---: | ---: | --- |
| `live-20260621-003152.wav` | 17.321 s | 4 | 3.832 s | 15.79% | Several short clause segments, content retained |
| `live-20260621-004142.wav` | 20.777 s | 3 | 2.523 s | 62.50% | Entire latter clause omitted |
| `live-20260621-082316.wav` | 9.534 s | 2 | 2.801 s | 9.68% | Content retained with punctuation differences |
| `live-20260621-122709.wav` | 13.865 s | 0 | 0 s | 100% | Short `嗯` utterance completely missed |

Aggregate CER is 35.71%. The two decisive baseline failures are the missing
latter half of `004142` and the zero-segment short utterance in `122709`.

## Neural VAD Evaluation

CrispASR Silero 6.2.0 (885 KB) was evaluated at thresholds 0.1, 0.2, 0.3,
0.4, and 0.5 on the same four recordings. The adapter uses the existing
`crispasr_vad_segments` ABI; no unrelated CrispASR backend was restored.

Important results:

- At 0.5, Silero produced no segment at all for `004142` and `122709`.
- At 0.2, it fragmented `004142` into six short regions; aggregate CER was
  36.51%, slightly worse than the 35.71% baseline.
- At 0.1, it covered the long quiet regions and reduced aggregate CER to
  15.87%. However, `003152` became one 14.34-second segment whose decode took
  6.355 seconds, compared with 3.832 seconds for the energy baseline's four
  progressive segments.
- No tested Silero threshold detected the short `嗯` recording.

Silero therefore improves transcript completeness only at a threshold that
materially worsens first-stable-text latency and still does not solve the short
utterance failure. It remains an explicit advanced/replay backend but is not
promoted to the production default from this dataset.

Raw result files:

- `/tmp/echoflow-vad-silero-0.1.jsonl`
- `/tmp/echoflow-vad-silero-0.2.jsonl`
- `/tmp/echoflow-vad-silero-segments.jsonl`

## Energy Threshold Evaluation

The current minimum RMS of 50 is the main reason quiet regions in `004142` are
discarded. A scan of speech ratios 1.5, 2.0, 2.5, 3.0, and 4.0 with minimum RMS
30 recovered speech through 18.08 seconds. Ratio 3.0 is the conservative choice:
it preserves the existing background-noise test (`RMS 40` over a `RMS 20`
floor remains non-speech) while retaining quiet `RMS 80` speech.

The ratio-2/RMS-30 transcript run reduced aggregate CER from 35.71% to 21.43%.
The ratio-3 segment boundaries are effectively identical on this sample set,
so ratio 3 is selected to reduce false activation risk. The increased total
decode work reflects recovered speech; with the new asynchronous worker it no
longer blocks capture.

## Candidate Evidence So Far

| Requirement | Evidence | Verdict |
| --- | --- | --- |
| Capture independent from ASR | 40-second slow-ASR queue test, exact sample equality | Pass in deterministic integration test |
| Ordered final text | 12 delayed segments return in input order | Pass |
| Partial stable updates | Callback fires before `finish()` with accumulated final segments | Pass |
| Reproducible live audio | Opt-in WAV contains exact S16 sample count and JSON sidecar | Pass in unit test |
| Better VAD | Neural backend not evaluated yet | Pending |
| Better real latency | Candidate live/replay comparison not run yet | Pending |
| Better long-dictation CER | 30/60-second labeled samples not run yet | Pending |

## Next Measurements

1. Build a labeled manifest containing quiet speech, short interjections,
   natural pauses, background noise, and 30/60-second dictation.
2. Replay the energy and Silero backends on identical PCM.
3. Tune one endpoint parameter at a time and retain negative results.
4. Run installed microphone capture with opt-in debug audio and compare sample
   count with wall-clock duration.
5. Complete acceptance tables for VAD miss rate, false activation, endpoint
   delay, first stable text, stop latency, CER, and boundary duplication.
