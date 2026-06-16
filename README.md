# echoflow
EchoFlow 声流输入法是一款面向 deepin 用户的离线语音输入法。

## 当前架构

EchoFlow 面向 deepin/Fcitx5 输入链路，不直接向应用模拟键盘输入。当前实现拆成三层：

- `fcitx-addon/`：Fcitx5 addon。跟踪任意输入框焦点和 cursor rect，捕获普通 `Ctrl` 长按/释放事件，把 `FOCUS x y w h`、`BLUR`、`CTRL_DOWN`、`CTRL_UP`、`TICK` 发送给 Python 服务；同时监听 `echoflow-fcitx.sock`，把识别结果提交到当前 input context。
- `echoflow/service.py`：Python 服务。维护长按状态机，长按阈值默认 350ms；达到阈值后启动 PipeWire 录音，松开后调用本项目配置的 Qwen ASR runner，再请求 Fcitx addon 上屏。
- `ui-host/` + `qml/EchoFlowTooltip.qml`：C++/Qt QML 宿主和 tooltip 界面。宿主监听 `echoflow-ui.sock`，根据 Fcitx cursor rect 把 tooltip 放到输入光标下方，显示“长按 Ctrl 语音输入”、录音中和转写中状态。这里刻意不用 PySide/PyQt，后续设置界面可以继续走 C++/Qt/DTK。

默认模型配置指向 Qwen ASR 0.6B：

```json
"model_name": "qwen-asr-0.6b",
"asr_runner": "qwen-asr-transcribe",
"asr_project_dir": "$HOME/AI/Model/Qwen3-ASR-GGUF",
"model_dir": "$HOME/AI/Model/Qwen3-ASR-GGUF/model-0.6B"
```

EchoFlow 默认优先使用 `model-0.6B`。如果本机按部分 Qwen3-ASR-GGUF 文档把 0.6B 解压到 `model/`，自检和转写会在 `model-0.6B` 不存在时自动兼容 `model/`。

`/home/hualet/projects/q/qwen-voice-input` 只作为离线 ASR、PipeWire 录音和 Fcitx 提交链路的技术参考；EchoFlow 不直接搬它的 evdev 触发器或 daemon 结构。模型、llama.cpp 构建产物、录音文件和 uv 虚拟环境不进入仓库。

## Python 环境

Python 依赖使用 `uv venv` 管理：

```bash
uv venv
uv run python -m unittest discover -s tests -v
uv run echoflow-service --print-default-config
uv run qwen-asr-transcribe --help
```

本地启动服务：

```bash
cp config.example.json config.json
./run.sh --config config.json
```

`run.sh` 会在缺少 `.venv` 时执行 `uv venv`，然后通过 `uv run` 启动 `echoflow-service`。

ASR 入口由本项目提供为 `qwen-asr-transcribe`。服务进程只把录音文件、模型目录、模型名和语言传给这个 runner；Qwen3-ASR-GGUF / llama.cpp 的加载细节集中在 `echoflow/asr_runner.py`，避免把参考项目 daemon 的内部结构带进主服务。

## 用户安装

从源码目录安装到当前用户：

```bash
./install-user.sh
```

如果 Qwen ASR 0.6B 模型和 llama.cpp 共享库还没准备好，可以先安装文件但暂不启动服务：

```bash
./install-user.sh --no-start
```

后续自检通过后再启动：

```bash
systemctl --user start echoflow.service echoflow-ui.service
```

安装脚本会完成这些步骤：

- 在 `$HOME/.local/share/echoflow/.venv` 创建 uv venv，并安装 Python 服务和 ASR runner。
- 构建并安装 Fcitx5 addon 到 `$HOME/.local`。
- 构建并安装 C++/Qt QML 宿主 `echoflow-ui` 到 `$HOME/.local/bin`，QML 文件安装到 `$HOME/.local/share/echoflow/qml`。
- 复制默认配置到 `$HOME/.config/echoflow/config.json`，不会覆盖已有配置。
- 安装 `echoflow.service` 和 `echoflow-ui.service` 用户服务；默认会启用并立即启动，`--no-start` 模式只启用不启动。
- 写入 `$HOME/.local/share/fcitx5/addon/echoflow.conf`，其中 `Library=` 指向实际安装的 addon 库路径。

## 准备 Qwen ASR 0.6B

EchoFlow 不把模型权重放进仓库。可以用脚本拉取 Qwen3-ASR-GGUF 项目并下载 0.6B 预转换模型：

```bash
./scripts/setup-qwen-asr-0.6b.sh
```

默认会写入：

```text
$HOME/AI/Model/Qwen3-ASR-GGUF
$HOME/AI/Model/Qwen3-ASR-GGUF/model-0.6B
```

脚本会创建 `qwen3_asr_llm.q5_k.gguf` 到 `qwen3_asr_llm.q4_k.gguf` 的兼容 symlink。它不会自动编译 llama.cpp；不同 deepin 机器的 GPU/Vulkan 情况不同，仍需要把匹配本机的 `libllama*.so`、`libggml*.so*` 放到：

```text
$HOME/AI/Model/Qwen3-ASR-GGUF/qwen_asr_gguf/inference/bin/
```

如果已经在本机编译好了 llama.cpp，可以用脚本把运行时共享库复制到 Qwen3-ASR-GGUF 项目里：

```bash
LLAMA_BUILD_DIR=$HOME/AI/Model/llama.cpp-build/build \
  ./scripts/install-llama-runtime.sh
```

脚本默认读取 `$HOME/AI/Model/Qwen3-ASR-GGUF`，也可以通过 `QWEN_ASR_PROJECT_DIR` 覆盖项目路径。它会检查 `libllama*.so*` 和 `libggml*.so*` 是否存在，缺失时直接返回非零。

配置好 Qwen ASR 0.6B 模型路径后，可以先跑运行时自检：

```bash
$HOME/.local/share/echoflow/.venv/bin/echoflow-service \
  --config $HOME/.config/echoflow/config.json \
  --self-test
```

自检会检查录音目录是否可创建、ASR runner、Qwen3-ASR-GGUF 项目目录、`qwen_asr_gguf/inference` Python 包路径、Qwen ASR 0.6B 模型目录、必需模型文件、`pw-record`、`libllama*.so*` / `libggml*.so*` 共享库、通知命令以及 Fcitx/UI socket 路径是否具备运行条件。模型目录、Qwen 包或 llama.cpp 共享库尚未配置完整时它会返回非零，并指出缺失项。

0.6B 模型目录至少需要包含：

```text
qwen3_asr_encoder_frontend.int4.onnx
qwen3_asr_encoder_backend.int4.onnx
qwen3_asr_llm.q4_k.gguf
```

自检通过后，可以先用一段已有 wav 音频验证 ASR 链路，不需要启动 Fcitx addon 或录音状态机：

```bash
$HOME/.local/share/echoflow/.venv/bin/echoflow-service \
  --config $HOME/.config/echoflow/config.json \
  --transcribe-file /path/to/sample.wav
```

这个命令会复用同一份配置里的 `asr_project_dir`、`model_dir`、`model_name` 和 `language`，调用 `qwen-asr-transcribe` 并把识别文本打印到 stdout。

卸载用户服务和已安装二进制：

```bash
./uninstall-user.sh
```

卸载脚本会保留 `$HOME/.config/echoflow` 和 `$HOME/.local/share/echoflow`，避免删除用户配置、模型路径设置和录音状态。

## 构建 QML 宿主

QML 宿主使用 C++/Qt，不使用 PySide/PyQt：

```bash
cmake -S ui-host -B build/ui-host -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/ui-host
```

本地运行：

```bash
build/ui-host/echoflow-ui --qml qml/EchoFlowTooltip.qml
```

安装后的 `echoflow-ui` 默认从 `${prefix}/share/echoflow/qml/EchoFlowTooltip.qml` 加载 QML；源码树里调试时可以用 `--qml qml/EchoFlowTooltip.qml` 覆盖。

## 构建 Fcitx5 Addon

需要系统已安装 Fcitx5 开发包。

```bash
cmake -S fcitx-addon -B build/fcitx-addon -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/fcitx-addon
```

CMake 会生成 `libechoflow.so` 和 addon 配置。用户安装时建议直接使用 `./install-user.sh`，它会把 addon 配置里的 `Library=` 改写为实际安装路径。

## 已验证

```bash
uv run python -m unittest discover -s tests -v
python3 -m py_compile echoflow/service.py echoflow/asr_runner.py echoflow/__main__.py tests/test_service.py tests/test_asr_runner.py tests/test_ui_host.py tests/test_install_scripts.py tests/test_model_setup_script.py
python3 -m json.tool config.example.json
bash -n install-user.sh uninstall-user.sh run.sh
bash -n scripts/setup-qwen-asr-0.6b.sh scripts/install-llama-runtime.sh
cmake -S fcitx-addon -B build/fcitx-addon -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/fcitx-addon
cmake -S ui-host -B build/ui-host -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/ui-host
```

当前 deepin 会话还验证过：

- `./install-user.sh --no-start` 可以安装 Python 服务、C++ QML 宿主和 Fcitx addon，`echoflow.service` / `echoflow-ui.service` 可以通过 systemd user 正常运行。
- Fcitx5 重启后能加载 `echoflow` addon，并创建 `/run/user/$UID/echoflow-fcitx.sock`。
- 已安装 venv 下的 `echoflow-service --self-test` 全部通过。
- Qwen ASR 0.6B + llama.cpp runtime 可以转写官方中文样例，输出 `甚至出现交易几乎停滞的情况。`。
- X11/deepin 会话中用真实 `Control_L` 长按触发了录音路径。
- 通过 Fcitx commit socket 向临时 Qt6 `QLineEdit` 提交唯一文本，控件 `textChanged` 收到了同一文本。
