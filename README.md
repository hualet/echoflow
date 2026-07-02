<p align="center">
  <img src="ui-host/icons/echoflow.svg" width="128" height="128" alt="EchoFlow logo">
</p>

<h1 align="center">EchoFlow</h1>

<p align="center">
  <strong>为 deepin 打造的离线语音输入法</strong>
</p>

<p align="center">
  <a href="https://github.com/hualet/echoflow/blob/main/LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0-blue" alt="License"></a>
  <a href="https://github.com/hualet/echoflow/actions/workflows/build.yml"><img src="https://img.shields.io/badge/build-deepin%2025.1-brightgreen" alt="Build"></a>
</p>

---

EchoFlow 让 deepin 用户**直接用语音输入中文**。不需要联网、不需要云端服务、不需要手动下载模型——按下右 `Ctrl`，说话，松开，识别结果就出现在当前输入框里。

### 为什么是 EchoFlow

EchoFlow 不止是语音转文字。它直接接入 Fcitx5 输入法框架，通过原生通道把识别结果提交到当前输入上下文——不依赖剪贴板，不模拟全局按键，就像你用拼音打出一段话一样精准。所有处理都在本机完成，你的声音永远不会离开你的设备。

## 亮点

- **离线识别，隐私安全**——录音和识别全部在本机完成，不经过任何云端服务。
- **右 Ctrl 即按即说**——焦点在输入框时，轻点右 `Ctrl` 开始录音，再点一下结束。无需离开键盘。
- **Fcitx5 原生上屏**——文本通过 Fcitx5 的 `commitString` 提交到当前输入框，不走剪贴板，不模拟按键，永远不会打错窗口。
- **模型提前加载，说出即识别**——模型在服务启动时加载并常驻内存，无冷启动等待。录音过程中后台分段转写，停止录音时大部分文字已经就绪。
- **录音中实时预览**——语音胶囊提示里显示临时转写结果，让你确认系统正在「听」你说话。
- **应用内一键下载模型**——设置界面中点击下载，支持 hf-mirror 和 HuggingFace 官方两种下载源，下载进度条清晰可见。
- **长语音分段处理**——长句自动切分为语音段落，后台并行转写，停止录音后快速返回整段文本。
- **系统主题自适应**——基于 DTK6 的托盘图标和设置界面，自动适配 deepin 浅色/深色主题。

## 快速开始

### 环境要求

- deepin 25.1 或兼容发行版
- Fcitx5 输入法框架
- PipeWire 音频服务

### 第一步：构建

```bash
git clone https://github.com/hualet/echoflow.git --recurse-submodules
cd echoflow
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

### 第二步：安装

```bash
./install-user.sh
```

安装完成后会自动启用两个用户 systemd 服务：
- `echoflow.service`——语音识别后台守护进程
- `echoflow-ui.service`——托盘图标和语音胶囊提示

本地安装会同时重载 Fcitx，使用户目录中的 addon 和对应的绝对模块路径
立即生效。使用 `--no-start` 时不会修改当前 Fcitx 会话，稍后需手动执行
`fcitx5 -rd`。

通过 deb 安装或升级时，addon 使用系统模块名 `libechoflow`。由于 Debian
维护脚本不能安全重启某个用户的桌面输入法会话，安装完成后需在该用户会话中
执行一次：

```bash
fcitx5 -rd
```

### 第三步：下载模型 & 开始使用

1. 点击托盘图标 → **设置** → **模型**，选择 `Qwen3-ASR-0.6B`（轻量）或 `Qwen3-ASR-1.7B`（高精度），点击**下载**。
2. 切换到任意可输入文本的窗口，确认输入法处于激活状态。
3. 轻点一下右 `Ctrl`，看到语音胶囊出现，开始说话。
4. 再点一下右 `Ctrl`，识别结果直接上屏。

## 工作流程

```
右 Ctrl 按下 ──► 开始录音 ──► Qwen ASR 实时转写 ──► 语音胶囊显示预览
                                                              │
                                         右 Ctrl 再按 / 点击暂停
                                                              │
                                                              ▼
                                    ┌── 后台分段最终转写 ──┐
                                    │                      │
                                    └── 合并文本 ──────────┘
                                                              │
                                                              ▼
                                              Fcitx5 提交到输入框
```

所有组件通过 Unix domain socket 在本机通信，无需网络：

| 组件 | 职责 |
| --- | --- |
| `echoflow-service` | 常驻守护进程：加载模型、管理录音、调度 ASR 转写 |
| `fcitx-addon` | Fcitx5 插件：捕获右 Ctrl 和输入焦点，接收并提交文本 |
| `echoflow-ui` | 桌面 UI：系统托盘、语音胶囊提示、设置对话框 |

## 进阶使用

### 常用运维命令

```bash
# 查看服务状态
systemctl --user status echoflow.service echoflow-ui.service

# 重启服务
systemctl --user restart echoflow.service echoflow-ui.service

# 环境自检
./build/service/echoflow-service --self-test

# 用已有音频文件验证模型
./build/service/echoflow-service --transcribe-file your_audio.wav

# 查看默认配置
./build/service/echoflow-service --print-default-config
```

### 配置文件

`~/.config/echoflow/echoflow.conf` 中的常用项：

| 配置项 | 说明 | 默认值 |
| --- | --- | --- |
| `basic.model.model_name` | 活动模型 | `qwen3-asr-0.6b` |
| `basic.model.mirror` | 模型下载源 | `hf-mirror` |
| `basic.recording.source` | 麦克风输入源 | 自动检测 |
| `basic.recording.min_record_seconds` | 最短录音时长 | `0.5` |
| `basic.recognition.stream_transcription` | 启用流式预览 | `true` |

修改配置后需重启服务生效：

```bash
systemctl --user restart echoflow.service echoflow-ui.service
```

### 卸载

```bash
./uninstall-user.sh
```

卸载会保留 `~/.config/echoflow` 和 `~/.local/share/echoflow` 下的用户数据和模型。

## 开发者

```bash
# 运行测试
ctest --test-dir build --output-on-failure

# 从构建目录运行
./run.sh

# 直接启动 UI
./build/ui-host/echoflow-ui
```

源码结构：

```
service/         守护进程、录音管线、ASR 引擎、状态机
fcitx-addon/     Fcitx5 插件（右 Ctrl 捕获、文本提交）
ui-host/         Qt6/DTK6 托盘和设置界面
qml/             语音胶囊 QML 动画
qwen-asr-runtime/  CMake 封装 qwen-asr C 运行时
tests/           QTest 逻辑测试、Shell spec 检查、性能基准
```

测试约束：逻辑测试不依赖模型权重、PipeWire 录音或运行中的 Fcitx。

### 发布性能基线

每次提升版本号前运行性能门禁。若上次记录的 CPU、模型、样本、配置和线程
设置均与当前机器一致，脚本复用历史基线；否则在当前机器以相同参数构建基线
版本和候选版本后再比较：

```bash
python3 scripts/check-release-performance.py \
  --baseline-ref v0.2.1 \
  --candidate-ref HEAD \
  --version 0.2.2 \
  --config "$HOME/.config/echoflow/echoflow.conf" \
  --model /path/to/model-or-model-directory \
  --manifest /path/to/release-manifest.json \
  --iterations 3 \
  --output docs/performance/releases/0.2.2.json
```

有历史 evidence 时增加
`--previous-evidence docs/performance/releases/<上一版本>.json`。聚合中位延迟
最多回退 10%，单样本最多回退 20%，CER 不得增加，必需样本不得产生空转写。

## 鸣谢

- [Fcitx5](https://github.com/fcitx/fcitx5)——输入法框架
- [qwen-asr](https://github.com/antirez/qwen-asr)——纯 C 的 Qwen ASR 运行时
- [Qwen](https://github.com/QwenLM/Qwen)——Qwen ASR 模型
- [xzl01/qwen-voice-input](https://github.com/xzl01/qwen-voice-input)——灵感来源

## License

GPL-3.0-or-later
