# qwen-asr Target Architecture Benchmark

## Summary

On the tested AMD Ryzen 7 6800H system, the portable `x86-64-v3` baseline and
`-march=native` produced no meaningful qwen-asr transcription speed difference.
The measured `native` build was slightly slower by 0.3% to 1.1%, which is within
normal run-to-run noise for this workload.

An additional AVX-512 test using `x86-64-v4` built successfully but could not
run the real ASR workload on this host. The process exited with code 132
(`SIGILL`) during model loading because the test CPU does not advertise AVX-512
support.

The result supports keeping `x86-64-v3` as the default EchoFlow qwen-asr build
baseline. It already enables the AVX2/FMA path used by the current kernels while
remaining portable across Haswell-era and newer x86_64 CPUs.

## Test Environment

- Date: 2026-06-19
- CPU: AMD Ryzen 7 6800H with Radeon Graphics
- Compiler: GCC 12.3.0
- Build type: `RelWithDebInfo`
- Model: `$HOME/.config/echoflow/qwen3-asr-0.6b`
- Benchmark target: `voice_latency_benchmark`
- Runtime mode: preloaded model, `--openblas-threads 4`

GCC target expansion on this machine:

- `-march=x86-64-v3`: generic tuning with AVX2, FMA, BMI2 enabled.
- `-march=native`: `-march=znver3 -mtune=znver3`, additionally enabling
  features such as SHA, VAES, VPCLMULQDQ, CLWB, and CLZERO.
- `-march=x86-64-v4`: generic tuning with AVX-512F, AVX-512BW, AVX-512CD,
  AVX-512DQ, and AVX-512VL enabled.

The extra `native` features are not expected to materially affect the current
ASR hot path, which is dominated by AVX2/FMA-capable qwen-asr kernels and
OpenBLAS work.

The tested CPU flags include AVX2 and FMA but no `avx512*` features, so the
`x86-64-v4` build is useful only as a compile check on this machine.

## Build Commands

```bash
cmake -S . -B /tmp/echoflow-bench-v3 \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DECHOFLOW_TARGET_ARCH=x86-64-v3

cmake -S . -B /tmp/echoflow-bench-native \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DECHOFLOW_TARGET_ARCH=native

cmake -S . -B /tmp/echoflow-bench-v4 \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DECHOFLOW_TARGET_ARCH=x86-64-v4

cmake --build /tmp/echoflow-bench-v3 \
  --target voice_latency_benchmark -j 16

cmake --build /tmp/echoflow-bench-native \
  --target voice_latency_benchmark -j 16

cmake --build /tmp/echoflow-bench-v4 \
  --target voice_latency_benchmark -j 16
```

## Benchmark Commands

Short sample, 2.37 seconds:

```bash
/tmp/echoflow-bench-v3/tests/voice_latency_benchmark \
  --transcribe-file "$HOME/.local/share/echoflow/recordings/voice-20260619-005648.wav" \
  --config "$HOME/.config/echoflow/echoflow.conf" \
  --model-dir "$HOME/.config/echoflow/qwen3-asr-0.6b" \
  --openblas-threads 4 \
  --iterations 6

/tmp/echoflow-bench-native/tests/voice_latency_benchmark \
  --transcribe-file "$HOME/.local/share/echoflow/recordings/voice-20260619-005648.wav" \
  --config "$HOME/.config/echoflow/echoflow.conf" \
  --model-dir "$HOME/.config/echoflow/qwen3-asr-0.6b" \
  --openblas-threads 4 \
  --iterations 6

/tmp/echoflow-bench-v4/tests/voice_latency_benchmark \
  --transcribe-file "$HOME/.local/share/echoflow/recordings/voice-20260619-005648.wav" \
  --config "$HOME/.config/echoflow/echoflow.conf" \
  --model-dir "$HOME/.config/echoflow/qwen3-asr-0.6b" \
  --openblas-threads 4 \
  --iterations 1
```

Longer sample, 13.01 seconds:

```bash
/tmp/echoflow-bench-v3/tests/voice_latency_benchmark \
  --transcribe-file "$HOME/.local/share/echoflow/recordings/voice-20260619-081230.wav" \
  --config "$HOME/.config/echoflow/echoflow.conf" \
  --model-dir "$HOME/.config/echoflow/qwen3-asr-0.6b" \
  --openblas-threads 4 \
  --iterations 4

/tmp/echoflow-bench-native/tests/voice_latency_benchmark \
  --transcribe-file "$HOME/.local/share/echoflow/recordings/voice-20260619-081230.wav" \
  --config "$HOME/.config/echoflow/echoflow.conf" \
  --model-dir "$HOME/.config/echoflow/qwen3-asr-0.6b" \
  --openblas-threads 4 \
  --iterations 4
```

## Results

The `x86-64-v4` build completed, and a synthetic service-only benchmark ran:

```json
{"mode":"session-synthetic","iteration":1,"stop_to_reply_ms":0.006,"record_stop_ms":0.000,"transcribe_ms":0.000,"commit_ms":0.001,"ui_messages":3,"reply":"COMMITTED"}
```

The real qwen-asr benchmark did not complete:

```text
[2026-06-19 14:56:50] loading qwen-asr model: /home/hualet/.config/echoflow/qwen3-asr-0.6b
exit code: 132
```

That exit code is the expected illegal-instruction failure for an AVX-512 binary
on this Ryzen 7 6800H host.

| Audio sample | `x86-64-v3` mean | `native` mean | Difference |
| --- | ---: | ---: | ---: |
| 2.37 s recording | 1808.2 ms | 1813.8 ms | `native` 0.31% slower |
| 2.37 s recording, excluding first iteration | 1785.5 ms | 1793.2 ms | `native` 0.43% slower |
| 13.01 s recording | 6284.8 ms | 6304.0 ms | `native` 0.31% slower |
| 13.01 s recording, excluding first iteration | 6220.7 ms | 6288.6 ms | `native` 1.09% slower |

Raw short-sample transcription timings:

| Iteration | `x86-64-v3` | `native` |
| ---: | ---: | ---: |
| 1 | 1921.713 ms | 1917.028 ms |
| 2 | 1794.502 ms | 1790.987 ms |
| 3 | 1812.695 ms | 1835.791 ms |
| 4 | 1790.823 ms | 1785.240 ms |
| 5 | 1756.404 ms | 1765.831 ms |
| 6 | 1773.069 ms | 1787.953 ms |

Raw longer-sample transcription timings:

| Iteration | `x86-64-v3` | `native` |
| ---: | ---: | ---: |
| 1 | 6477.188 ms | 6350.497 ms |
| 2 | 6261.019 ms | 6212.577 ms |
| 3 | 6252.072 ms | 6131.613 ms |
| 4 | 6149.017 ms | 6521.500 ms |

## Interpretation

The benchmark does not show a practical advantage for `-march=native` on this
hardware. Both builds use the same qwen-asr AVX2/FMA-capable code path, and the
remaining `native` differences are either not used by the ASR kernels or are too
small to overcome normal scheduling, CPU frequency, and OpenBLAS variability.

Keeping `ECHOFLOW_TARGET_ARCH=x86-64-v3` therefore preserves portable release
builds without giving up measurable local performance on the tested machine.
An AVX-512 build needs to be benchmarked on real AVX-512 hardware before making
any claim about its performance.
