# CrispASR 上游同步、Vulkan 复评与内存剖析

Date: 2026-10-11

把 CrispASR 子模块从 fork 的旧基线（与上游分叉点为 2026-05-12，最后一次同步约在
2026-06-23）更新到上游 `CrispStrobe/CrispASR` 的 `1e42e543`（2026-10-10），并在
其上重新应用 EchoFlow 需要的本地补丁。机器环境与
[qwen3-asr-cpu-decode-optimization.md](qwen3-asr-cpu-decode-optimization.md) 相同：
Ryzen 7 6800H，0.6B Q4_K，8 线程。

结论：

- **同步上游后，松手延迟再降约 28–30%**。中文、英文样本的识别文字与旧版逐字一致；
  合成的变调重复音频 CER 上升 0.7 个百分点。
- **Vulkan（Radeon 680M）在新版上不再比 CPU 快**，还要额外锁定约 1 GB GTT 内存，
  暂不启用。
- **内存没有因为同步而下降**。最大的可优化项仍是 encoder 的计算缓冲。

## 子模块变化

上游改写过历史，fork 与上游各有数千个提交互不包含。按提交标题比对后，真正只属于
fork 的提交只有三个。它们在新分支 `echoflow-rebase` 上基于 `upstream/main` 重新应用：

| 提交 | 说明 |
|---|---|
| `fix(build): support qwen3-only static embedding` | 重新实现：`CRISPASR_QWEN3_ONLY` 时跳过除 `qwen3_asr.h` 外的所有后端头文件探测；`LLAMA_CORE_DIR` 改为相对 CrispASR 自身路径 |
| `fix(vad): fail closed on compute errors` | 原样 cherry-pick |
| `perf(qwen3-asr): use native GQA in KV-cached attention` | 原样 cherry-pick；上游 qwen3 仍是 `GQA_MANUAL_CONT` |
| `fix(c-api): guard backend-only session fields in slim builds` | 新增：上游三处 TTS 字段和函数只在对应后端存在时才声明，使用处却没有条件编译，导致精简构建编译失败 |

EchoFlow 侧的配套改动：

- 原先在子模块里裁剪后端链接；现在改由顶层 `CMakeLists.txt` 对 `crispasr-lib` 的链接
  依赖使用**白名单**（`qwen3_asr|crisp_audio|crispasr-core|ggml*|Threads`）。上游这段
  链接代码很长且改动频繁，白名单让以后同步上游时不再冲突。
- `CRISPASR_NO_C2PA_NATIVE=ON`：上游新增的 C2PA 签名器依赖另一个嵌套子模块，EchoFlow
  用不到。
- 上游把 ggml 改成了嵌套子模块，所以初始化子模块时必须用 `--recursive`。CI 和 README
  本来就是这样做的；`AGENTS.md` 里遗留的 `third_party/qwen-asr` 命令已改正。

## 速度与准确性

### 整段识别（`voice_latency_benchmark`，1 次预热 + 5 次取中位数）

| 音频 | 旧版 | 上游版 | 变化 | 识别文字 |
|---|---:|---:|---:|---|
| zh 1.5 s | 429 ms | 268 ms | -38% | 一致 |
| zh 3 s | 630 ms | 423 ms | -33% | 一致 |
| zh 6 s | 1221 ms | 832 ms | -32% | 一致 |
| zh 13 s | 2645 ms | 1852 ms | -30% | 一致 |
| en 11 s | 2168 ms | 1577 ms | -27% | 一致 |
| multi 31.5 s | — | 5027 ms | — | 见下 |

\* multi 31.5 s 旧版这一轮的测量受到系统 swap 干扰（9.5 s，而同一二进制此前测得
6.2 s），不作比较。

13 s 中文分阶段（临时计时，未提交）：

| 阶段 | 旧版 | 上游版 |
|---|---:|---:|
| mel | 131 ms | 64 ms |
| encoder | 1075 ms | 703 ms |
| prefill | 594 ms | 388 ms |
| decode | 719 ms | 644 ms |

上游在这段时间里把 ggml 合并到了 v0.26，并重写了 encoder 前端。提速主要在 encoder 和
prefill 这两个计算密集阶段。具体由哪个提交带来，没有逐个 bisect。

两个上游开关在本机上都没有可测量的影响：`CRISPASR_GGUF_REPACK=0`（关闭 CPU repack 缓冲）
和 `CRISP_AUDIO_WINDOWED_ATTN=1`（encoder 窗口化注意力）。因此保持默认。

### 实际使用路径（`vad_replay_benchmark`，Silero VAD，3 轮取中位数）

| 音频 | 松手延迟 旧版 → 上游版 | 首段出字 | CER 旧版 → 上游版 |
|---|---|---|---|
| zh 13 s | 655 → **456 ms**（-30%） | 5.25 → 5.00 s | 10.77% → 10.77% |
| en 11 s | 715 → **514 ms**（-28%） | 2.67 → 2.57 s | 5.61% → 5.61% |
| multi 31.5 s | 1191 → **861 ms**（-28%） | 2.67 → 2.53 s | 21.11% → 21.81% |

multi 31.5 s 是 4 遍几乎相同的句子，其中两遍是人工变调。上游版在其中一遍变调朗读里
把后半句识别成了 "and do for you ask"，旧版是完整句子，这就是 CER 上升 0.7 个百分点的
原因。中文和英文自然语音样本的识别文字与旧版逐字一致。

## Vulkan 复评

与上一轮相同的 Vulkan 构建，换到上游代码后重新测量：

| 音频 | CPU | Vulkan | Vulkan 额外 GTT |
|---|---:|---:|---:|
| zh 1.5 s | 264 ms | 288 ms | +976 MB |
| zh 3 s | 440 ms | 449 ms | +985 MB |
| zh 6 s | 867 ms | 892 ms | +931 MB |
| zh 13 s | 1836 ms | 1989 ms | +1022 MB |
| en 11 s | 1572 ms | 1546 ms | +960 MB |
| multi 31.5 s | 4249 ms | 4731 ms | +1043 MB |

旧版 CPU 上，Vulkan 能快 20–26%；上游版 CPU 快了约 30% 之后，Vulkan 已经持平甚至更慢。
它还要锁定约 1 GB 不可换出的 GTT 内存，再加上约 0.35 s 的启动开销，所以**暂不启用**。
识别文字与 CPU 逐字一致。

构建前提另作记录：需要 `glslc` 和 `spirv-headers`；在这台 13 GB 内存、swap 已用
10 GB 的机器上，并行编译 Vulkan shader 时 `-j12` 会因内存不足被系统杀掉，`-j4` 可以编过。

## 内存剖析

识别过程中进程的内存峰值（CPU，上游版）：

| 音频 | RSS | 其中匿名内存 | 其中文件映射 |
|---|---:|---:|---:|
| 1.5 s | 630 MB | 197 MB | 431 MB |
| 3 s | 661 MB | 227 MB | 431 MB |
| 6 s | 744 MB | 310 MB | 432 MB |
| 13 s | 962 MB | 527 MB | 432 MB |
| 31.5 s | 1323 MB | 889 MB | 432 MB |

用临时诊断代码打印的各缓冲区实际大小（旧版，同一结构）：

| 部分 | 大小 | 说明 |
|---|---|---|
| 模型权重 | 510 MB | mmap 映射文件，属于可回收的页缓存；实际常驻约 430 MB |
| encoder 计算缓冲 | 3 s 75 MB、13 s 351 MB、31 s 803 MB | **随音频长度线性增长（约 27 MB/s），而且分配后不释放** |
| 融合 QKV 权重 | 63 MB | 加速用的额外拷贝 |
| KV 缓存 | 虚拟 448 MB，常驻约 30 MB | 固定按 4096 个 token 分配；布局决定只有写过的页才常驻 |
| LLM 计算缓冲 | 2–14 MB | |

实际使用时 VAD 最长 8 s 强制切段，所以 encoder 缓冲最多约 220 MB，但它会一直占着，
直到进程退出。

### 剩余机会（按收益排序，均需改 CrispASR）

1. **encoder 前端分批计算**：卷积前端按 1 s 一块独立计算，但所有块放在一张图里一起算。
   改成固定批大小，峰值能从每秒约 27 MB 降到一个与音频长度无关的常数，理论上识别结果
   不变。
2. **空闲时释放计算缓冲**：服务绝大部分时间在等待按键。识别结束后释放 encoder 和 LLM
   的调度缓冲，代价是下次识别要重新分配（预计数十毫秒，需实测）。
3. **去掉融合 QKV 副本**：省 63 MB，需要先评估速度损失。
4. **Vulkan 的 GTT 占用**：如果以后在 Vulkan 更有优势的机器上启用，需要先查清这约
   1 GB GTT 的来源。上游的 `e144dd03`（encoder 只加载音频塔权重）没有让它下降。
