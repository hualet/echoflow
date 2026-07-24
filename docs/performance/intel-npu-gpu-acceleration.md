# Intel NPU / Arc GPU 加速支持调研

Date: 2026-07-22
Branch: `main`（调研分支，未合入）

调研在 Intel Core Ultra 9 285H（Arrow Lake-H）平台上，为 EchoFlow 的
CrispASR（qwen3-asr-0.6B Q4_K GGUF）引入 Intel iGPU/NPU 加速的可行性与收益。
结论：**SYCL + Arc iGPU 可用且有效，NPU 本代（Meteor Lake 类）不在 ggml-sycl 支持范围内；
推荐落地 SYCL/Arc GPU 路线，并在服务侧把 ASR 模型常驻**。

## 硬件与平台

| 项 | 规格 |
|---|---|
| CPU | Intel Core Ultra 9 285H，16 核（P/E 混合），无 HT，睿频 5.4 GHz |
| 指令集 | AVX2 + FMA + F16C + **AVX_VNNI**；**无 AVX-512、无 AMX** |
| iGPU | Arrow Lake-P 集显（Arc Pro 130T/140T），128 EUs，共享内存 ~30 GB |
| NPU | Arrow Lake GNA + Meteor Lake NPU（`/dev/accel/accel0`，内核驱动 `intel_vpu`） |
| 内存 | 30 GB LPDDR |
| OS | Deepin 25（glibc 2.38），内核 6.18.36 |

## 软件栈（本次实测版本）

- 编译器：Intel oneAPI DPC++/C++ Compiler **2026.1.0**（`icpx`，`-fsycl`）
- compute-runtime **26.27.39122.11**（NEO + level-zero GPU driver）
- IGC **2.38.2**，GMMLIB **22.10.0**，level-zero loader **1.32.0**
- 模型：`qwen3-asr-0.6b-q4_k.gguf`（540 MB），CrispASR qwen3 backend，`threads=8`
- 测试音频：jfk（11s 英文）、paraformer_zh（13s 中文）、multispeaker（31.5s）

## 测试方法

两组口径，都用 `tests/benchmarks/voice_latency_benchmark`：

- **冷启动（cold）**：每次新建进程，含模型加载。反映"一次性转录"或命令行调用。
- **热启动（warm）**：单进程内模型常驻，1 轮预热后取 5 轮中位数。**这是 EchoFlow
  服务进程的实际工作模式**（daemon 常驻、Ctrl 触发），是判断落地价值的主口径。

## 结果一：CPU 上开启 AVX_VNNI 专用 kernel 的收益（热启动）

EchoFlow 顶层 `CMakeLists.txt` 在 `ECHOFLOW_CPU_TARGET=x86-64-v3` 时强制
`GGML_AVX_VNNI OFF`（为保证跨机器二进制兼容）。本机 CPU 支持 AVX_VNNI，
对照测试显式开启 `GGML_AVX_VNNI` 后的收益：

| Clip | baseline (native) | +AVX_VNNI kernel | 提升 |
|---|---|---|---|
| jfk (11s) | 2320 ms | 2170 ms | 6.5% |
| paraformer_zh (13s) | 2740 ms | 2733 ms | 0.2% |
| multispeaker (31.5s) | 6679 ms | 6678 ms | 0.0% |

结论：在 285H 上 **AVX_VNNI 专用 kernel 基本无收益**（≤6.5%，长音频为 0）。
原因是 `-march=native` 已经让通用代码路径用上了 AVX_VNNI 指令，ggml 的手写
AVX_VNNI kernel 相对通用路径没有额外优势。**当前 `CMakeLists.txt` 关闭
AVX_VNNI 的取舍在本机是合理的**，换不来性能。

## 结果二：SYCL + Arc iGPU vs CPU（热启动，主口径）

通过 `GGML_SYCL=ON` + `icpx` 编译 ggml-sycl 后端，`ONEAPI_DEVICE_SELECTOR=level_zero:gpu`
把整个模型跑在 Arc iGPU 上：

| Clip | CPU baseline | SYCL Arc GPU | GPU 加速比 |
|---|---|---|---|
| jfk (11s) | 2320 ms | 1566 ms | **1.48x** |
| paraformer_zh (13s) | 2740 ms | 1887 ms | **1.45x** |
| multispeaker (31.5s) | 6679 ms | 5701 ms | **1.17x** |

趋势：**短音频加速比更高（~1.45–1.48x）**，长音频加速比下降到 1.17x。
长音频里 segment 切分、调度、host↔device 数据搬运占比变大，掩盖了纯计算收益。

准确率：CPU 与 SYCL 转录结果**文字完全一致**（paraformer_zh 仅句中一处标点差异），
SYCL 浮点精度无损。

## 结果三：冷启动下 GPU 反而更慢

| Clip | CPU cold (per-process) | SYCL cold (per-process) |
|---|---|---|
| jfk | ~2675 ms | ~4350 ms |

冷启动 SYCL 比 CPU **慢约 1.6x**：每次新进程要重新把模型 offload 到 GPU、
IGC 重新 JIT 编译所有 SYCL kernel（kernel 缓存不跨进程）。**这正说明 GPU 路线
只在模型常驻的服务模式下有意义**，对"每次新进程跑一次"的命令行场景是负优化。

## NPU 的结论：本代不支持，别投入

1. ggml 的 `ggml-sycl` 后端只面向 Intel **GPU**（level-zero GPU driver），
   不覆盖 NPU。SYCL `sycl-ls` 列出的设备也只有 `level_zero:gpu` 和 `opencl:cpu/gpu`，
   **NPU 不在 SYCL 可见设备里**。
2. Intel NPU 的官方栈是 OpenVINO（`DEVICE.NPU`），但 ggml-openvino 后端要求
   把 GGUF 模型转成 OpenVINO IR；Qwen3-ASR 是 encoder-decoder，能否在 NPU 上
   跑通、跑得更快都没有先例，工程风险高。
3. 本代 NPU（Meteor Lake / Arrow Lake，~11 TOPS）算力也远小于 iGPU（128 EUs），
   即便跑通也不太可能比 Arc GPU 快。

**建议：NPU 这条线在 285H 这代硬件上不做。** 等 Lunar Lake / Panther Lake
带独立 NPU 内存和高 TOPS 的平台，且 OpenVINO 生态成熟后再评估。

## 落地建议（如果要上 SYCL/Arc GPU）

代码改动很小，主要是构建系统和服务初始化：

1. **构建侧**：新增 `ECHOFLOW_SYCL=ON` 选项，开启时
   - 用 `icx/icpx` 作为 C/C++ 编译器
   - `set(GGML_SYCL ON)`、`set(GGML_SYCL_TARGET INTEL)`
   - `tests/CMakeLists.txt` 与 `service/CMakeLists.txt` 的
     `echoflow_enable_dead_strip` 需把 `IntelLLVM` 加入编译器 ID 匹配
     （当前只认 `GNU|Clang`，导致 `--gc-sections` 不生效）
   - SYCL 链接需要 `--unresolved-symbols=ignore-in-object-files`，
     因为 EchoFlow 把 crispasr 裁剪到只剩 qwen3 后端，`crispasr_c_api.cpp`
     里残留的 grammar/LID/diarize 符号引用在 SYCL/lld 下无法被
     `--gc-sections` 完全剥离（GNU ld 可以）。这一点值得上游反馈或单独处理。

2. **服务侧**：`CrispSession` 走 `crispasr_session_open_explicit(path, "qwen3", nThreads)`，
   SYCL 后端在模型加载时由 ggml-sycl 自动把全部层 offload 到 GPU（无需传 ngl 参数），
   服务进程常驻即满足"模型常驻"。无需改 `CrispSession` 接口。

3. **运行环境**：用户机器需装 oneAPI DPC++ 编译时栈 + compute-runtime/level-zero
   运行时。**这是落地的主要摩擦点**——Intel graphics 的 apt 源目前锁在
   compute-runtime 24.26（对应内核 6.8 时代），在 Deepin 25 / 内核 6.18 上
   `zeInit` 会段错误。必须从 GitHub release 装 compute-runtime 26.27+ 和
   IGC 2.38+，且需匹配 glibc ≥ 2.38。安装链路对普通用户偏重，建议打包时
   做成可选依赖、或在 README 给出明确步骤。

4. **打包**：SYCL 构建产物依赖 `libsycl.so`、`libmkl_sycl_blas.so`、
   `libze-intel-gpu.so` 等一众 oneAPI/Intel graphics 库，体积大、版本敏感。
   短期不建议进主 deb，可作为独立 `echoflow-sycl` 变体或社区构建说明。

## 性能预期总结

| 场景 | 配置 | jfk 延迟 | 评价 |
|---|---|---|---|
| 当前 release（CPU native） | 现状 | 2320 ms | 基线，够用 |
| CPU + AVX_VNNI kernel | 改 CMake | 2170 ms | 不值得改 |
| SYCL + Arc iGPU（模型常驻） | 新增 SYCL 变体 | **1566 ms** | **值得做，快 1.48x** |
| SYCL + Arc iGPU（冷启动） | 同上 | 4350 ms | 负优化，不要冷启 |
| NPU | — | 未跑通 | 本代硬件不做 |

## 复现步骤

```bash
# 在 285H 机器上（Deepin 25，内核 6.18+）
# 1. 装 oneAPI DPC++ + 最新 compute-runtime（详见上文版本）
sudo apt install intel-oneapi-compiler-dpcpp-cpp intel-oneapi-mkl-devel
# compute-runtime 26.27 + IGC 2.38 + level-zero 1.32 从 GitHub release 装 deb

# 2. 配置 SYCL 构建
source /opt/intel/oneapi/setvars.sh
CC=icx CXX=icpx cmake -S . -B build-sycl \
  -DCMAKE_BUILD_TYPE=Release -DECHOFLOW_BUILD_TESTS=ON \
  -DGGML_SYCL=ON -DGGML_SYCL_TARGET=INTEL
cmake --build build-sycl --target voice_latency_benchmark -j$(nproc)

# 3. 跑热启动 benchmark（主口径）
ONEAPI_DEVICE_SELECTOR=level_zero:gpu \
  ./build-sycl/tests/voice_latency_benchmark \
  --transcribe-file bench-audio/jfk.wav --iterations 6

# 4. CPU 对照
cmake -S . -B build-cpu -DCMAKE_BUILD_TYPE=Release -DECHOFLOW_BUILD_TESTS=ON
cmake --build build-cpu --target voice_latency_benchmark -j$(nproc)
./build-cpu/tests/voice_latency_benchmark \
  --transcribe-file bench-audio/jfk.wav --iterations 6
```

测试音频从 `third_party/crispasr/samples/` 转成 16 kHz mono s16le WAV。

---

# 附录：测试机环境改动记录

本次调研在测试机 `zccrs@10.20.8.72`（Intel Core Ultra 9 285H）上留下的全部
改动，供事后清理或复现核对用。总占用约 7 GB，主体是 `/opt/intel` 的 oneAPI
工具链。

## 一、系统级改动（需 sudo，apt 安装）

### 新增 apt 源与签名 key

| 文件 | 内容 |
|---|---|
| `/etc/apt/sources.list.d/oneAPI.list` | Intel oneAPI apt 源 |
| `/etc/apt/sources.list.d/intel-graphics.list` | Intel graphics jammy unified 源 |
| `/usr/share/keyrings/oneapi-archive-keyring.gpg` | oneAPI 源签名 key |
| `/usr/share/keyrings/intel-graphics.gpg` | Intel graphics 源签名 key |

### apt 安装的包

**oneAPI 工具链**（落在 `/opt/intel/`，约 5.0 GB）：

- `intel-oneapi-compiler-dpcpp-cpp-2026.1`（DPC++/C++ 编译器，核心）
- `intel-oneapi-mkl-devel 2026.1`（MKL SYCL BLAS，ggml-sycl INTEL target 必需）
- `intel-oneapi-tbb`、`intel-oneapi-openmp`、`intel-oneapi-libdpstd-devel`、
  `intel-oneapi-dpcpp-debugger`、`intel-oneapi-umf`、`intel-oneapi-tcm`、
  `intel-oneapi-dev-utilities` 等依赖包

**Intel GPU 运行时**（从 GitHub release 下 deb 安装，非 apt 源）：

- `intel-opencl-icd 26.27.39122.11`（NEO compute-runtime）
- `libze-intel-gpu1 26.27.39122.11`（level-zero GPU driver）
- `intel-igc-core-2 2.38.2`、`intel-igc-opencl-2 2.38.2`（IGC 编译器）
- `libze1 1.32.0`、`libze-dev 1.32.0`（level-zero loader）
- `intel-ocloc 26.27.39122.11`、`libigdgmm12 22.10.0`

> 注：先从 Intel graphics apt 源装了旧版 `intel-level-zero-gpu 1.3.26241`
> 等（对应内核 6.8 时代），在 Deepin 25 / 内核 6.18 上 `zeInit` 段错误。
> 已先卸载旧版，再装上述新版。详见正文「落地建议」一节。

**系统编译依赖**：

- `g++-13`、`libstdc++-13-dev`（给 icpx 提供 C++ 标准库头文件）

### 用户组

`zccrs` 用户被加入 `render`、`video` 组（GPU 渲染节点 `/dev/dri/renderD128`
的访问权限）。

### level-zero 配置

- `/etc/level-zero/intel-gpu.json`（compute-runtime 包自带，调研中曾手动建过
  临时版，后由新版 deb 覆盖）

### apt 缓存

`/var/cache/apt/archives/`（约 1.3 GB，apt 下载缓存，可安全清理）。

## 二、用户目录文件（无需 sudo）

| 路径 | 大小 | 说明 |
|---|---|---|
| `~/projects/echoflow/` | 277 MB | rsync 过去的源码副本 + 3 个 build 目录（`build-cpu-baseline`、`build-cpu-avxvnni`、`build-sycl`）+ benchmark 结果 + 几个 shell 脚本。源码副本上打了临时补丁（见下），未触碰原 git 仓库 |
| `~/.config/echoflow/` | 516 MB | 模型 `qwen3-asr-0.6b-q4_k.gguf` + 一个软链 |
| `~/neu-pkgs/` | 93 MB | 从 GitHub 下的 8 个 compute-runtime / IGC / level-zero deb（已安装，可删） |
| `~/projects/echoflow/bench-audio/` | — | 3 个测试音频（jfk、paraformer_zh、multispeaker，16 kHz mono s16le） |
| `/tmp/zetest.cpp`、`/tmp/zetest`、`/tmp/echoflow.conf` | — | level-zero 诊断小程序和临时配置 |

### 源码副本上的临时补丁（仅作用于 `~/projects/echoflow`，非原仓库）

这些改动是为让 benchmark 在缺 Fcitx5 / Dtk6 dev 包的本机能编出来、以及让
SYCL 能链接通过而打的临时补丁，**没有提交、没有推回原仓库**：

- 顶层 `CMakeLists.txt`：fcitx-addon / ui-host 收进 `ECHOFLOW_BUILD_FCITX_ADDON`
  开关下（默认 OFF）；测 AVX_VNNI 时临时把 `GGML_AVX_VNNI` 改 ON
- `tests/CMakeLists.txt`：Dtk6 `find_package` 改 QUIET；`test_onboarding_dialog`
  加 `if(Dtk6_FOUND)` guard；`echoflow_enable_dead_strip` 认 `IntelLLVM`；
  benchmark target 加 `--unresolved-symbols=ignore-in-object-files`
- `service/CMakeLists.txt`：`echoflow_enable_dead_strip` 认 `IntelLLVM`

## 清理选项

- **保留复跑能力**：留 `/opt/intel`、compute-runtime/IGC/level-zero 运行时、
  模型、源码副本。删 `~/neu-pkgs`、`/var/cache/apt/archives`、`/tmp/*`，
  约省 1.4 GB。
- **完全清理**：apt 卸载全部 oneAPI + Intel graphics 包、删两个 apt 源与 key、
  删 `~/projects/echoflow`、`~/.config/echoflow`、`~/neu-pkgs`，从 render/video
  组移除 zccrs。
- **保持现状**：不做任何改动。
