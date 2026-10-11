# Qwen3-ASR CPU 推理延迟剖析与优化

Date: 2026-10-11

在 AMD Ryzen 7 6800H 上对 EchoFlow 默认模型（Qwen3-ASR-0.6B Q4_K，CrispASR
qwen3 后端）做分阶段剖析，定位到两处可以直接落地的 CPU 优化：

1. **decode 注意力改用原生 GQA**：CrispASR 的 qwen3 KV-cache 注意力原先每生成
   一个 token，就在 28 层里把整段 KV 缓存复制、展开到 16 个 query head 再复制
   一次，单 token 耗时随上下文线性增长。改为 `GQA_NATIVE`，由
   `ggml_flash_attn_ext` 直接广播 KV head。
2. **CrispASR 线程数按物理核数选择**：默认值从固定 6 改为"物理核数，上限 8"。

结论：在实际使用的 VAD 分段路径上，**松手到出字的等待时间降低 19–24%**，所有
样本的识别文字和 CER 完全不变。整段识别（13 s 中文）从 3.72 s 降到 2.60 s
（-30%）。

## 环境

| 项 | 值 |
|---|---|
| CPU | AMD Ryzen 7 6800H，8 核 16 线程，AVX2/FMA/F16C，无 AVX-512 |
| iGPU | Radeon 680M（本次未使用） |
| 内存 | 13 GB |
| OS / 编译器 | deepin 25，GCC 12.3，`RelWithDebInfo`，`ECHOFLOW_CPU_TARGET=native` |
| 模型 | `qwen3-asr-0.6b-q4_k.gguf`（540 MB） |
| CrispASR 基线 | `ae2f91a6` |
| 音频 | `third_party/crispasr/samples/` 中的 `paraformer_zh.wav`（13 s，另截 1.5/3/6 s）、`jfk.wav`（11 s）、`multispeaker.wav`（31.5 s） |

## 分阶段剖析

在 `crispasr_session_transcribe()` 的 qwen3 路径上临时加计时（未提交），拆成
mel → encoder → prefill → decode 四段。基线配置（6 线程，`GQA_MANUAL_CONT`）：

| 音频 | mel | encoder | prefill | decode | 合计 |
|---|---:|---:|---:|---:|---:|
| 1.5 s | 14 ms | 166 ms | 154 ms（48 token） | 123 ms（7 token） | ≈450 ms |
| 3 s | 28 ms | 256 ms | 201 ms（61 token） | 216 ms（11 token） | ≈700 ms |
| 6 s | 55 ms | 534 ms | 354 ms（100 token） | 534 ms（21 token） | ≈1.48 s |
| 13 s | 122 ms | 1275 ms | 763 ms（204 token） | 1329 ms（38 token） | ≈3.5 s |

观察：

- encoder 和 prefill 是计算密集型：单线程 6.1 s / 3.5 s，8 线程 1.08 s / 0.61 s，
  基本随线程数线性扩展。
- decode 几乎不随线程数变化，单 token 约 17 ms（上下文约 50）到 35 ms（上下文
  约 240），**随上下文长度线性增长**。在 decode 单步里再拆分：建图约 0.3 ms、
  调度分配约 0.5 ms，其余约 34 ms 全在计算本身。
- mel 是单线程，约 9 ms/s 音频，占比很小。

## 发现一：decode 注意力反复复制整段 KV 缓存

`core_attn::kv_self_attn()` 的 `GQA_MANUAL_CONT` 模式，每层每步要做：

```
ggml_cont(K 缓存视图) → repeat_4d 到 16 头 → ggml_cont   （V 同样一遍）
```

Qwen3-0.6B 是 16 个 query head、8 个 KV head、head_dim 128、28 层。上下文 240
时，每生成一个 token 要额外搬动约 28 × 6 × 1 MB 的数据，并多出约 170 个需要
线程同步的图节点。这就是单 token 耗时随上下文增长的来源。

`ggml_flash_attn_ext` 原生支持 GQA（KV head 数整除 Q head 数时直接广播），
所以 qwen3 改用 `GQA_NATIVE` 即可省掉全部复制。注释里提到的 Vulkan
"缺 f16 REPEAT" 问题，只在手动展开路径上出现，原生路径反而能绕开。

## 发现二：线程数

改用原生 GQA 后，13 s 中文整段识别：

| 线程 | 1 | 4 | 6（原默认） | 8 | 12 | 16 |
|---|---:|---:|---:|---:|---:|---:|
| 耗时（3 次中位数） | 11.26 s | 3.72 s | 3.02 s | **2.65 s** | 2.85 s | 3.51 s |

超过物理核数后，超线程兄弟核会拖慢计算密集的 encoder/prefill。

新默认值 `crispThreadsForPhysicalCores()`：读
`/sys/devices/system/cpu/cpu*/topology` 统计物理核数，取 `min(物理核, 8)`；
读不到时回退到 `hardware_concurrency()/2`，再不行用 4。本机得到 8；
Intel 285H（16 核、无超线程）也是 8，与
[intel-npu-gpu-acceleration.md](intel-npu-gpu-acceleration.md) 中使用的线程数一致。
用户在 `advanced.crisp.threads` 里显式配置的值仍然优先。

## 结果

### 整段识别（`voice_latency_benchmark`，1 次预热 + 5 次取中位数）

改动前、改动后两个二进制只差一行 `gqa_mode`，交替运行：

| 音频 | 改前 6 线程 | 改后 6 线程 | 改前 8 线程 | **改后 8 线程** | 总变化 |
|---|---:|---:|---:|---:|---:|
| zh 1.5 s | 469 ms | 442 ms | 432 ms | **401 ms** | -15% |
| zh 3 s | 733 ms | 671 ms | 666 ms | **600 ms** | -18% |
| zh 6 s | 1507 ms | 1335 ms | 1339 ms | **1145 ms** | -24% |
| zh 13 s | 3724 ms | 2979 ms | 3521 ms | **2603 ms** | -30% |
| en 11 s | 2897 ms | 2385 ms | 2552 ms | **2088 ms** | -28% |
| multi 31.5 s | 11872 ms | 7737 ms | 10212 ms | **6163 ms** | -48% |

各次运行的离散度在 ±2% 以内。音频越长、生成的 token 越多，GQA 改动的收益越大。

### 实际使用路径（`vad_replay_benchmark`，Silero VAD，3 轮取中位数）

这是更接近用户感受的口径：VAD 边录边切段识别，"松手延迟"是录音结束后还要
等的时间。

| 音频 | 指标 | 改前 6 线程 | **改后 8 线程** | 变化 |
|---|---|---:|---:|---:|
| zh 13 s | 松手延迟 | 808 ms | **651 ms** | -19% |
| en 11 s | 松手延迟 | 845 ms | **669 ms** | -21% |
| multi 31.5 s | 松手延迟 | 1495 ms | **1129 ms** | -24% |
| 三条 | 首段出字 | 2.77 / 5.50 / 2.79 s | 2.67 / 5.24 / 2.66 s | -3~-5% |
| 三条 | CER | 5.61% / 10.77% / 21.11% | 完全相同 | 0 |

首段出字时间主要由 VAD 何时切出第一段决定，所以变化不大。

### 准确性

- **GQA 改动**：同一线程数下，6 条音频的改前改后识别文字逐字一致。
- **线程数**：整段一次性识别 `multispeaker.wav`（4 遍几乎相同的句子：原声、
  变调、原声、变调）时，只有 6 线程识别出全部 4 遍；4、5、7、8、10、12 线程都
  稳定漏掉 1 遍。这是模型在高度重复长音频上对浮点归约顺序敏感，6 线程只是碰巧
  对了，并不说明 6 线程更准。在实际使用的分段路径上，6 线程与 8 线程都完整识别
  出 4 遍，CER 相同。发布性能门禁里那条 34.6 s 重复口述样本，固定用 6 线程运行
  （见下文），不受影响。

## 其他改动

- `voice_latency_benchmark --threads N` 之前被静默忽略，参数实际没有生效。现在
  会覆盖 `crispThreads`。`scripts/check-release-performance.py` 默认传
  `--threads 6`，因此发布门禁的 benchmark 身份现在与实际运行线程数一致，历史
  基线仍可复用。

## 剩余机会（按预期收益排序）

1. **Vulkan + Radeon 680M**：剩下的大头 encoder（13 s 音频约 1.07 s）和 prefill
   （约 0.6 s）都是纯计算。Intel Arc 上 SYCL 已测得短音频约 1.45 倍加速。本机已有
   Mesa RADV 驱动和 `libvulkan-dev`，还缺 `glslc`。需要运行时探测或作为可选项，
   不能进入 x86-64-v3 通用包的默认路径。
2. **录音期间预先跑 encoder**：Qwen3-ASR 的 encoder 按窗口处理，可以在录音时
   增量编码最后一段，松手后只剩 prefill + decode。需要扩展 CrispASR 会话接口。
3. **mel 多线程**：约 9 ms/s 音频，收益小。
4. **decode 单步固定开销**：上下文很短时每 token 仍约 15 ms，主要是约 800 个
   图节点的逐节点线程同步；可以考虑复用 decode 图或合并 QKV（已有
   `CRISPASR_QWEN3_ASR_FUSED_QKV` 开关，未评估）。

## 复现

```bash
cmake --build build --target voice_latency_benchmark vad_replay_benchmark

./build/tests/voice_latency_benchmark \
  --transcribe-file third_party/crispasr/samples/paraformer_zh.wav \
  --config ~/.config/echoflow/echoflow.conf --threads 8 --iterations 6

./build/tests/vad_replay_benchmark --manifest MANIFEST.json \
  --backend silero --vad-model ~/.config/echoflow/vad/silero-v6.2.0-ggml.bin \
  --config ~/.config/echoflow/echoflow.conf
```

改动前后的 A/B 对比：分别在 `gqa_mode` 为 `GQA_MANUAL_CONT` 和 `GQA_NATIVE`
时构建 benchmark，复制出两个二进制后交替运行。
