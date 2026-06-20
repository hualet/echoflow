# Streaming ASR Engine Comparison

Date: 2026-06-20

## Summary

EchoFlow needs low-latency voice input, but the evaluated streaming ASR engines
do not provide a final-text replacement for the current Qwen ASR path.

The practical conclusion is:

- Use Qwen ASR as the source of final committed text.
- Do not commit concatenated streaming partial text as the final result.
- If streaming UI is kept, treat partial text as preview only.
- For a better stop-time experience, split recording into speech segments and
  run Qwen final decoding per segment, rather than waiting for one full-session
  decode at stop time.
- If a separate streaming preview engine is introduced, sherpa-onnx is the best
  fit among the tested options because it is native-friendly and lightweight.

## Goal

The intended user experience is:

1. While the user keeps speaking, the capsule continuously updates with the
   current spoken text.
2. When recording stops, the final text appears quickly.
3. The final text is accurate enough to commit directly into the focused input
   context.

The key question was whether another inference library can provide reliable
streaming partial output that can simply be concatenated and committed, avoiding
the slow whole-audio final Qwen decode.

## Test Environment

Worktree:

```text
/home/hualet/projects/hualet/echoflow/.worktrees/live-stream-asr
```

Temporary evaluation environment:

```text
/tmp/echoflow-asr-eval
```

Python dependencies were installed with `uv`. The final temporary virtualenv
size after FunASR and CPU PyTorch installation was about 1.5 GB.

## Test Samples

The tests used existing EchoFlow recordings from:

```text
~/.local/share/echoflow/recordings/
```

The main samples were:

| File | Duration |
| --- | ---: |
| `voice-20260619-005209.wav` | 4.948 s |
| `voice-20260619-081230.wav` | 13.012 s |
| `voice-20260619-210058.wav` | 8.297 s |

These samples are useful because they reflect the actual microphone path and
the real phrasing used while testing EchoFlow.

## Baseline: Qwen ASR

Command pattern:

```bash
./build/service/echoflow-service \
  --transcribe-file ~/.local/share/echoflow/recordings/<sample>.wav \
  --config ~/.config/echoflow/echoflow.conf
```

Results:

| File | Time | Text |
| --- | ---: | --- |
| `voice-20260619-005209.wav` | 2.842 s | `测试一下输入，测试一下输入。` |
| `voice-20260619-081230.wav` | 7.447 s | `说一段很长的文字，是不是时间相对会更长一点？这次要好好试一下，这样以后做语音输入的时候就知道有个心理预期了。` |
| `voice-20260619-210058.wav` | 4.885 s | `输入输入输入输入，则是，则是，则是，则是，能不能输出？能不能输出？` |

Qwen remains the strongest final-text engine in these samples. Its weakness is
not quality, but stop-time latency when the whole recording is decoded after
the user stops.

## sherpa-onnx

Official docs:

- <https://k2-fsa.github.io/sherpa/onnx/pretrained_models/index.html>
- <https://github.com/k2-fsa/sherpa/blob/master/docs/source/onnx/pretrained_models/online-ctc/zipformer-ctc-models.rst>

Models tested:

| Model | Local size |
| --- | ---: |
| `sherpa-onnx-streaming-zipformer-small-ctc-zh-int8-2025-04-01` | 26 MB |
| `sherpa-onnx-streaming-zipformer-ctc-zh-int8-2025-06-30` | 156 MB |

The 26 MB model was extremely fast but produced unusable text on the main
sample, for example repeated `叔叔叔...`.

The 156 MB model was much better and is the more representative candidate:

| File | Processing time | RTF | First text at audio time | Final streaming text |
| --- | ---: | ---: | ---: | --- |
| `voice-20260619-005209.wav` | 0.648 s | 0.131 | 1.6 s | `测试一下输入测试一下输入给` |
| `voice-20260619-081230.wav` | 1.592 s | 0.122 | 1.6 s | `一段很长的文字是不是时间相对会更长一点这次要好好试一下这样以后做入的时候就知道有出心理预期了` |
| `voice-20260619-210058.wav` | 1.096 s | 0.132 | 1.8 s | `输入输入输入输入测试测试测试测试能能输出输出` |

Assessment:

- Very good latency.
- Simple native integration path through ONNX Runtime / sherpa-onnx C++ APIs.
- Model size is acceptable for an optional preview engine.
- Output has no punctuation and still has insertions, omissions, and wrong
  words.
- It is suitable for live preview, but not as the default final commit engine.

## FunASR Paraformer Streaming

Official docs and model page:

- <https://github.com/modelscope/FunASR>
- <https://modelscope.github.io/FunASR/>
- <https://huggingface.co/funasr/paraformer-zh-streaming>

Model tested:

```text
paraformer-zh-streaming
```

Runtime and model footprint:

| Component | Size / Cost |
| --- | ---: |
| FunASR + CPU PyTorch temporary venv | 1.5 GB |
| Downloaded streaming Paraformer model | 849 MB |
| First model download + construction | 83.198 s |
| Cached model construction | 4.759 s |

Streaming config used:

```python
chunk_size = [0, 10, 5]
chunk_stride = chunk_size[1] * 960  # 600 ms at 16 kHz
encoder_chunk_look_back = 4
decoder_chunk_look_back = 1
```

Results:

| File | Processing time | RTF | First text at audio time | Final concatenated streaming text |
| --- | ---: | ---: | ---: | --- |
| `voice-20260619-005209.wav` | 1.455 s | 0.294 | 1.2 s | `测试一下输入测试一下输入` |
| `voice-20260619-081230.wav` | 3.982 s | 0.306 | 1.2 s | `所以段很长的文字是不是时间相对会更长一点这次要好好试一下将以后做语音输入的时候就知道有的心理憋气了` |
| `voice-20260619-210058.wav` | 2.406 s | 0.290 | 1.8 s | `输入输入输入输入测试测试测试测试能们能输出哼不能输出` |

Assessment:

- Better than sherpa-onnx on the shortest sample.
- Still produces wrong words on longer or repetitive speech.
- Streaming partial concatenation is not final-quality text.
- Runtime footprint is too large for EchoFlow's current native C++ service path.
- Integration would likely require a Python sidecar service, a separate model
  manager, and additional packaging work.

FunASR is a credible ASR toolkit, but it is not a good default replacement for
EchoFlow's current Qwen-based final transcription.

## Vosk

Official docs:

- <https://alphacephei.com/vosk/>
- <https://github.com/alphacep/vosk-api>
- <https://alphacephei.com/vosk/models>

Vosk has a true offline streaming API and supports Chinese. The Python package
installed quickly, but the small Chinese model download from the official model
host was extremely slow in this environment. The download was stopped after the
ETA stayed around 20 minutes.

Assessment:

- The API shape fits streaming.
- It is lightweight and offline-friendly.
- The Chinese small model class is unlikely to beat Qwen or FunASR in final text
  quality.
- It may be worth revisiting only if a reliable model mirror and a focused
  Chinese quality benchmark are needed.

## whisper.cpp

Official docs:

- <https://github.com/ggerganov/whisper.cpp/blob/master/examples/stream/README.md>
- <https://github.com/ggml-org/whisper.cpp>

The whisper.cpp real-time example is explicitly a simple streaming demo: it
samples microphone audio repeatedly and runs transcription continuously. That
is a sliding-window strategy rather than a native incremental decoder.

Assessment:

- Good offline final transcription engine in many contexts.
- The common streaming mode is not the same as stable incremental ASR.
- It risks overlap stitching errors and repeated text.
- It does not solve EchoFlow's requirement to commit reliable final text quickly
  from partials.

## Findings

### Streaming partial text is not final text

The core issue is not only implementation. It is also a model/decoding tradeoff:
low-latency streaming output has less right context than offline final decoding.
For Chinese continuous speech, repeated phrases, and uncertain boundaries, early
partials are expected to be revised or corrected later.

The tested engines confirm this:

- sherpa-onnx returns text quickly, but final streaming text still has omissions
  and insertions.
- FunASR streaming is stronger on short speech, but still makes wrong-word
  errors on longer speech.
- Qwen final decoding is slower, but produces the most natural final text.

Therefore concatenating partials and committing them is not reliable.

### Stop-time latency should be solved by segmentation

The current pain is that the user records continuously and then waits for a
whole-session final decode after stop. A better architecture is:

1. Use VAD / endpoint detection to split the recording into speech segments
   during recording.
2. Run Qwen final decoding per completed segment.
3. Show committed/stable segment text separately from unstable live preview.
4. On stop, only finalize the current open segment.

This preserves Qwen's final-text quality while bounding stop-time latency.

## Recommendation

Recommended default path:

1. Keep Qwen ASR as the final commit engine.
2. Disable committing concatenated streaming partials as final text.
3. If the streaming capsule remains enabled, label and treat it as preview.
4. Implement speech segmentation so that completed segments can be finalized
   before the user stops recording.

Optional future path:

1. Add sherpa-onnx as an optional preview engine if live feedback quality is
   more important than preview accuracy.
2. Do not use sherpa-onnx output as committed text unless the user explicitly
   chooses a low-latency / lower-accuracy mode.
3. Avoid FunASR as the default native service dependency unless EchoFlow accepts
   a heavy Python/Torch sidecar and a much larger package footprint.

## Final Decision

No tested alternative should replace Qwen ASR for final text.

The most practical product design is a two-tier pipeline:

```text
live preview:  optional lightweight streaming engine or Qwen live partials
final commit:  Qwen final decode on short VAD-delimited segments
```

This gives the user continuous feedback without pretending that partial text is
stable enough to commit.
