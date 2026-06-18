# AGENTS.md

Offline voice input method for deepin/Fcitx5. Toggle recording by tapping the
**right** `Ctrl` (press once to start, press again to stop), record with
PipeWire, transcribe locally with qwen-asr, and commit through a Fcitx5 addon.
Keep the service, input-method addon, and UI host boundaries clean.

## Stack

- C++17, CMake >= 3.16, one top-level build.
- `service/` builds the `echoflow-service` daemon and the
  `libechoflow_service.a` logic library.
- `third_party/qwen-asr/` is the ASR submodule. `qwen-asr-runtime/` builds it as
  `libqwen_asr.a` with OpenBLAS.
- `fcitx-addon/` builds the Fcitx5 addon that captures right `Ctrl`, observes
  input-context focus/typing events, and commits text.
- `ui-host/` builds the Qt6/DTK tray + tooltip host.
- Recording: PipeWire (`pw-record`). ASR: antirez `qwen-asr` safetensors model.
- No Python runtime, no llama.cpp, no GGUF/ONNX runtime, no packaging, no CI, no
  pre-commit.

Fresh clones must initialize:

```bash
git submodule update --init --recursive third_party/qwen-asr
```

## Commands

```bash
# Configure, build every native component, and run all tests
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure

# CLI sanity checks
./build/service/echoflow-service --print-default-config
./build/service/echoflow-service --self-test
bash -n install-user.sh uninstall-user.sh scripts/*.sh tests/spec/*.sh
sh -n run.sh

# Run locally from the build tree
./run.sh

# Install for the current user
./install-user.sh
./install-user.sh --no-start

# Prepare the default qwen-asr 0.6B safetensors model
./scripts/setup-qwen-asr-0.6b.sh
```

## Architecture — boundaries you must respect

- `service/` owns the daemon state machine, recording lifecycle, ASR invocation,
  Fcitx commit client, UI notifications, control-socket server, CLI modes, and
  self-test.
- `service/AsrEngine.h` / `service/AsrEngine.cpp` are the **only** service
  files that know qwen-asr's C API. Do not include `qwen_asr.h` elsewhere.
- `service/VoiceSession.*` depends on `IRecorder`, `IAsrEngine`, `ICommitter`,
  and `IUiNotifier`; keep this logic unit-testable without PipeWire, qwen-asr,
  Fcitx, or the UI host.
- `fcitx-addon/` owns input-context events and right-`Ctrl` capture; it talks to
  `echoflow-service` over `echoflow-control.sock` and receives commits through
  `echoflow-fcitx.sock`.
- `ui-host/` + `qml/EchoFlowTooltip.qml` own the tooltip and tray/settings UI;
  they listen on `echoflow-ui.sock`.
- Runtime sockets live under `/run/user/$UID/`: `echoflow-control.sock`,
  `echoflow-fcitx.sock`, and `echoflow-ui.sock`.

## Testing quirks

- Logic tests are QTest binaries in `tests/*.cpp`; they link
  `libechoflow_service.a`.
- Bash spec-as-test lives in `tests/spec/run_spec.sh`; it checks scripts,
  CMake wiring, settings schema, protocol verbs, and stale dependency removal.
- Tests do not require model weights, PipeWire capture, or a running Fcitx
  daemon.
- `test_committer` uses a real Unix datagram socket loopback server.
- Full verification is `cmake --build build && ctest --test-dir build
  --output-on-failure`.

## Conventions & gotchas

- Every source file carries:
  `SPDX-FileCopyrightText: 2026 Hualet Wang` and
  `SPDX-License-Identifier: GPL-3.0-or-later`.
- Do not commit model weights, recordings, build directories, or installed
  runtime artifacts. Models live outside the repo, by default under
  `$HOME/AI/Model/qwen3-asr-0.6b`.
- Config lives at `~/.config/echoflow/echoflow.conf`. Paths inside use `$HOME`
  expansion. `install-user.sh` writes a default config only if it does not
  already exist.
- `model-0.6B` and `model/` fallback handling still exists for compatibility,
  but the default model path is `$HOME/AI/Model/qwen3-asr-0.6b`.
- `install-user.sh` installs binaries and services but does not restart Fcitx;
  after addon changes, restart Fcitx manually with `fcitx5 -rd`.

## Code style

- C++: classes `PascalCase`; methods `camelCase`; members use trailing
  underscore; constants use `kCamelCase`.
- Prefer existing local interfaces and small, testable classes over broad
  helpers.
- Add comments only when they clarify non-obvious behavior.

## Logging

- C++ daemon: use `echoflow::log()` from `service/log.h` for timestamped stdout
  logs. Do not scatter `std::cout` logging.
- C++ addon: use `FCITX_INFO` / `FCITX_WARN` / `FCITX_ERROR`.

## Git & commits

- Commit only when the user asks.
- Before committing: `git status --short`, stage only intended files, and never
  stage model weights, recordings, build output, or installed artifacts.
- Subject: concise, imperative, describing the actual change. Body: explain why
  and what, wrapped around 72 columns.
