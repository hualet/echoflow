# echoflow

EchoFlow 声流输入法是一款面向 deepin 用户的离线语音输入法。只需轻点一下右 `Ctrl`，即可在当前输入框内用语音快速录入文字；录音、识别、上屏全程在本地完成，无需联网。

## 项目简介

在日常打字场景中，语音输入往往是最快的方式，但主流方案大多依赖云端服务，存在隐私和稳定性的顾虑。EchoFlow 希望为 deepin/Fcitx5 用户提供一条纯粹的本地语音输入链路：

- 按下右 `Ctrl` 开始录音，再按一次结束录音；
- 录音通过 PipeWire 采集，识别由本地 Qwen ASR 模型完成；
- 识别结果通过 Fcitx5 直接提交到当前输入框；
- 屏幕上的 tooltip 会实时提示录音、转写状态。

项目采用 Python 服务 + C++ Fcitx5 插件 + C++/Qt QML tooltip 三层架构，模型与运行时不进入仓库，用户可按需准备。

## 项目亮点

- **完全离线**：语音数据只在本地处理，不上传云端。
- **一键切换**：右 `Ctrl` 单击即可开始 / 停止录音，不改变原有键盘习惯。
- **本地 ASR**：基于 Qwen3-ASR-GGUF 0.6B 模型，配合 llama.cpp 本地推理。
- **深度集成**：作为 Fcitx5 addon 工作，直接向当前输入上下文上屏，不模拟全局键盘事件。
- **轻量提示**：Qt6 QML tooltip 跟随光标，显示录音与转写状态。

## 构建安装

### 1. 准备环境

需要系统已安装：

- Python >= 3.11
- [uv](https://github.com/astral-sh/uv)
- Qt6 开发包（Core/Gui/Qml/Quick/Widgets）
- Fcitx5 开发包
- CMake >= 3.16
- PipeWire（运行时依赖，`pw-record`）

### 2. 一键安装到当前用户

```bash
./install-user.sh
```

如果模型尚未准备好，可以先只安装文件而不启动服务：

```bash
./install-user.sh --no-start
```

安装脚本会自动完成：创建 uv 虚拟环境、构建并安装 Fcitx5 addon、构建并安装 QML 宿主、写入默认配置、安装 systemd 用户服务。

### 3. 准备 Qwen ASR 0.6B 模型

EchoFlow 不把模型权重放进仓库。运行脚本拉取项目并下载预转换模型：

```bash
./scripts/setup-qwen-asr-0.6b.sh
```

默认写入：

```text
$HOME/AI/Model/Qwen3-ASR-GGUF
$HOME/AI/Model/Qwen3-ASR-GGUF/model-0.6B
```

你还需要把本机编译好的 llama.cpp 共享库放到：

```text
$HOME/AI/Model/Qwen3-ASR-GGUF/qwen_asr_gguf/inference/bin/
```

如果已有 llama.cpp 构建目录，可以用：

```bash
LLAMA_BUILD_DIR=$HOME/AI/Model/llama.cpp-build/build \
  ./scripts/install-llama-runtime.sh
```

### 4. 从源码运行（开发调试）

```bash
cp config.example.json config.json
./run.sh --config config.json
```

`run.sh` 会在缺少 `.venv` 时自动创建虚拟环境。

## 使用说明

### 启动服务

安装完成后，systemd 用户服务会在登录时自动启动。也可以手动管理：

```bash
systemctl --user start echoflow.service echoflow-ui.service
systemctl --user enable echoflow.service echoflow-ui.service
```

### 语音输入

1. 将焦点放到任意可输入文本的位置；
2. 轻点一下键盘右 `Ctrl`，屏幕 tooltip 提示“录音中”；
3. 说完后，再点一下右 `Ctrl` 结束录音；
4. 等待 tooltip 显示“转写中”，随后识别结果会自动上屏。

### 运行自检

配置好模型路径后，建议先跑自检：

```bash
$HOME/.local/share/echoflow/.venv/bin/echoflow-service \
  --config $HOME/.config/echoflow/config.json \
  --self-test
```

自检会检查录音目录、ASR runner、模型文件、`pw-record`、llama.cpp 共享库、通知命令以及 socket 路径是否具备运行条件。

### 用已有音频验证 ASR

不需要启动 Fcitx addon，也可以直接转写一段 wav 音频：

```bash
$HOME/.local/share/echoflow/.venv/bin/echoflow-service \
  --config $HOME/.config/echoflow/config.json \
  --transcribe-file /path/to/sample.wav
```

### 卸载

```bash
./uninstall-user.sh
```

卸载会保留 `$HOME/.config/echoflow` 和 `$HOME/.local/share/echoflow`，避免误删用户配置与录音状态。

## 鸣谢

EchoFlow 的开发离不开以下核心开源项目与参考实现：

- [Fcitx5](https://github.com/fcitx/fcitx5) —— 输入法框架与 addon 生态。
- [Qwen3-ASR-GGUF](https://github.com/QwenLM/Qwen3-ASR-GGUF) / [Qwen](https://github.com/QwenLM/Qwen) —— 本地语音识别的核心模型与推理实现。
- [llama.cpp](https://github.com/ggml-org/llama.cpp) —— GGUF 模型本地推理运行时。

特别感谢 [xzl01/qwen-voice-input](https://github.com/xzl01/qwen-voice-input) 在离线 ASR、PipeWire 录音与 Fcitx 提交链路上提供的技术参考。

## License

EchoFlow 采用 GNU General Public License v3.0 或更高版本授权（GPL-3.0-or-later）。
