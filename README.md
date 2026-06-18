# echoflow

EchoFlow 声流输入法是一款面向 deepin/Fcitx5 用户的离线语音输入工具。把焦点放在输入框内，轻点右 `Ctrl` 开始录音，再点一次结束录音，识别结果会通过 Fcitx5 直接上屏。

## 特性

- 完全本地处理：录音、识别和上屏都在本机完成。
- Fcitx5 原生提交：不模拟全局按键，直接提交到当前输入上下文。
- C++ 常驻服务：`echoflow-service` 保持 qwen-asr 模型 resident，避免每次录音重新加载。
- Qt6/DTK UI：托盘设置入口和录音/转写 tooltip。
- 单一构建链路：根目录 CMake 一次构建 service、qwen-asr runtime、Fcitx addon 和 UI host。

## 依赖

需要系统安装：

- CMake >= 3.16
- C++17 编译器
- Qt6 Core/Gui/Qml/Quick/Widgets 开发包
- DTK6 Widget/Core/Gui 开发包
- Fcitx5 Core/Utils 开发包
- PipeWire 运行时工具 `pw-record`
- OpenBLAS 开发包（Debian/deepin: `libopenblas-dev`）

初始化 qwen-asr 子模块：

```bash
git submodule update --init --recursive third_party/qwen-asr
```

## 构建与测试

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

常用 CLI 检查：

```bash
./build/service/echoflow-service --print-default-config
./build/service/echoflow-service --self-test
```

## 准备模型

EchoFlow 不把模型权重放进仓库。默认使用 qwen-asr 0.6B safetensors 模型：

```bash
./scripts/setup-qwen-asr-0.6b.sh
```

默认安装到：

```text
$HOME/AI/Model/qwen3-asr-0.6b
```

安装后可用已有 wav 文件验证：

```bash
./build/service/echoflow-service --transcribe-file third_party/qwen-asr/samples/jfk.wav
```

## 安装

安装到当前用户：

```bash
./install-user.sh
```

只安装并 enable systemd 用户服务，不立即启动：

```bash
./install-user.sh --no-start
```

安装内容包括：

- `$HOME/.local/bin/echoflow-service`
- `$HOME/.local/bin/echoflow-ui`
- Fcitx5 addon `libechoflow.so`
- `~/.config/systemd/user/echoflow.service`
- `~/.config/systemd/user/echoflow-ui.service`
- 默认配置 `~/.config/echoflow/echoflow.conf`（仅在不存在时创建）

如果更新了 Fcitx addon，需要手动重启 Fcitx：

```bash
fcitx5 -rd
```

## 开发运行

```bash
./run.sh --print-default-config
./run.sh --self-test
./run.sh
```

UI host 可直接运行：

```bash
./build/ui-host/echoflow-ui
```

## 配置

配置文件位于：

```text
~/.config/echoflow/echoflow.conf
```

关键项：

- `advanced.runtime.model_dir`: qwen-asr safetensors 模型目录。
- `basic.recognition.language`: 识别语言，默认 `Chinese`。
- `basic.recognition.prompt`: 可选提示词，用于术语纠正。
- `basic.recognition.strip_trailing_punctuation`: 是否去除识别结果尾部标点。
- `advanced.storage.recordings_dir`: 录音保存目录。

修改配置后重启服务：

```bash
systemctl --user restart echoflow.service echoflow-ui.service
```

## 使用

1. 将焦点放到任意文本输入框。
2. 轻点右 `Ctrl` 开始录音。
3. 说完后再轻点右 `Ctrl`。
4. 等待 tooltip 显示转写状态，识别结果随后上屏。

## 卸载

```bash
./uninstall-user.sh
```

卸载会保留 `~/.config/echoflow` 和 `~/.local/share/echoflow`，避免误删用户配置和录音。

## 鸣谢

- [Fcitx5](https://github.com/fcitx/fcitx5)
- [qwen-asr](https://github.com/antirez/qwen-asr)
- [Qwen](https://github.com/QwenLM/Qwen)
- [xzl01/qwen-voice-input](https://github.com/xzl01/qwen-voice-input)

## License

GPL-3.0-or-later.
