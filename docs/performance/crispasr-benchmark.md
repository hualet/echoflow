# CrispASR (qwen3-asr-0.6B GGUF) Benchmark

Date: 2026-06-24
Branch: `test/crispasr-qwen3-gguf`

Benchmarks [CrispASR](https://github.com/CrispStrobe/CrispASR) v0.8.3 (qwen3
backend, `cstr/qwen3-asr-0.6b-q4_k.gguf`, 515 MiB) against EchoFlow's current
antirez pure-C `qwen-asr` runtime (Qwen3-ASR-0.6B safetensors).

## Setup

- CPU: 16-core x86-64 (Deepin). Threads: crispasr `-t 8`, qwen via OpenBLAS.
- Both cold-load the model per run (fresh process). Wall time includes load.
- Audio: 16 kHz mono s16le WAV.
- CrispASR LID auto-download disabled by passing `-l <lang>` (the `ggml-tiny.bin`
  LID step otherwise hangs on network and is unnecessary when the language is
  known).

## Batch transcription (file mode)

| Clip                  | Lang | Audio | qwen wall | CrispASR wall | Speedup |
|-----------------------|------|-------|-----------|---------------|---------|
| jfk                   | en   | 11.0s | 10.79s    | 4.78s         | 2.3x    |
| 10s_back_down_the_road | en  | 10.0s | 7.51s     | 3.93s         | 1.9x    |
| samples_test_speech   | en   | 3.6s  | 5.70s     | 2.56s         | 2.2x    |
| tts (Mandarin)        | zh   | 4.4s  | 6.16s     | 2.78s         | 2.2x    |
| voice-note            | zh   | 1.4s  | 3.25s     | 1.91s         | 1.7x    |

CrispASR is ~2x faster across the board.

## Accuracy (representative)

- jfk (en): both correct.
- tts (zh): **CrispASR better** — returned the complete sentence
  `你好，我很嫉妒你的声音，所以请表现得更加自然一些。` with punctuation; qwen
  truncated the final word (`...自然一�`).
- samples_test_speech (en): both approximate the brand name ("Voicestral" vs
  "Vox Troll" for "Voxtrail"); comparable.

## Streaming (`--stream --stream-json`, for the live pipeline)

Native per-token streaming works. Tuning matters:

- **`redecode` + `--stream-final-on-silence-ms 1500`** (recommended): clean
  single final for jfk, accurate, progressive partials. First partial lands
  early.
- Default `redecode` + 800 ms over-segments jfk into 3 utterances ("Ask not."
  split off).
- **`prefix` mode is unsuitable**: fast but duplicates text ("What My fellow
  Americans. Ask not. What ...") when the rolling window evicts audio — a
  documented caveat.

Streaming wall time when audio is fed faster than realtime exceeds batch
(re-encode cost per final); in realtime live use the partials stream as the user
speaks and per-utterance inference (RTF ~0.3) keeps up. CrispASR's native
streaming + FireRed VAD replaces EchoFlow's energy-VAD segmenter in the
replacement design.

## Verdict

CrispASR wins: ~2x lower latency, at-least-as-good accuracy (better on Mandarin),
and a single 515 MiB GGUF (vs multi-file safetensors). It clears the verdict gate
→ proceed to in-service replacement of `qwen-asr-c`.

## Repro

```
CRISP=~/projects/CrispASR/build/bin/crispasr
GGUF=~/.config/echoflow/crisp/qwen3-asr-0.6b-q4_k.gguf
# batch
$CRISP -m "$GGUF" --backend qwen3 -l en -f jfk.wav -t 8
# streaming
ffmpeg -i jfk.wav -f s16le -ar 16000 -ac 1 jfk.raw
cat jfk.raw | $CRISP --stream --stream-json -m "$GGUF" --backend qwen3 -l en \
  --vad --stream-final-on-silence-ms 1500 --stream-final-mode redecode -t 8
```
