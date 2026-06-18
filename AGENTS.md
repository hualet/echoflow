# AGENTS.md

Offline voice input method for deepin/Fcitx5. Toggle recording by tapping the
**right** `Ctrl` (press once to start, press again to stop), recorded with
PipeWire, transcribed by local Qwen ASR, committed to the focused input context
through a Fcitx5 addon. Three layers must stay cleanly separated.

## Stack

- Python >= 3.11 via `uv`; console scripts `echoflow-service`
  (`echoflow.service:main`) and `qwen-asr-transcribe` (`echoflow.asr_runner:main`).
- C++17, CMake >= 3.16: `fcitx-addon/` (Fcitx5 addon) and `ui-host/` (Qt6
  Core/Gui/Qml/Quick/Widgets + DTK6 Widget/Core/Gui).
- `third_party/llama.cpp/` (git submodule pinned at commit `dd9280a`, tag
  `b9106`) + `llama-runtime/CMakeLists.txt` (thin CMake wrapper). Fresh
  clones must run `git submodule update --init --recursive third_party/llama.cpp`.
- Recording: PipeWire (`pw-record`). ASR: Qwen3-ASR-GGUF (llama.cpp + onnxruntime).
- No packaging (no debian/linglong), no CI, no pre-commit.

## Commands

```bash
# Tests (unittest, NOT pytest — `pytest tests/` is not wired up)
uv run python -m unittest discover -s tests -v

# Single module / case
uv run python -m unittest tests.test_service -v
uv run python -m unittest tests.test_service.MainCliTests.test_transcribe_file_prints_asr_text_without_starting_service

# Run service locally (auto-creates .venv if missing via `uv venv`, then `uv run`)
./run.sh

# CLI sanity checks (these are the real "lint/typecheck" — there is no CI)
uv run echoflow-service --print-default-config
uv run qwen-asr-transcribe --help
python3 -m py_compile echoflow/*.py tests/*.py
bash -n install-user.sh uninstall-user.sh run.sh scripts/*.sh

# Build the two C++ components (need Qt6 + Fcitx5 dev packages)
cmake -S ui-host   -B build/ui-host   -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build/ui-host
cmake -S fcitx-addon -B build/fcitx-addon -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build/fcitx-addon

# Build llama.cpp runtime (default backend AUTO: Vulkan + glslc if present, else CPU).
# Override with -DECHOFLOW_LLM_BACKEND=Vulkan|CPU.
git submodule update --init --recursive third_party/llama.cpp
cmake -S llama-runtime -B build/llama-runtime -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DQWEN_ASR_PROJECT_DIR="$HOME/AI/Model/Qwen3-ASR-GGUF" && cmake --build build/llama-runtime
cmake --install build/llama-runtime   # copies libllama*.so* / libggml*.so* into qwen_asr_gguf/inference/bin/
```

## Architecture — boundaries you must respect

- `echoflow/service.py` — toggle state machine, recording, commit. The
  long-running daemon. Entry point `echoflow-service` (`echoflow.service:main`).
- `echoflow/asr_runner.py` — the **only** module that knows the Qwen3-ASR-GGUF
  Python API. The service calls it as a **subprocess** (`qwen-asr-transcribe`,
  `echoflow.asr_runner:main`) so model runtime stays isolated. Do not import
  Qwen classes from `service.py`.
- `fcitx-addon/` (C++) — owns input-context events and **right** `Ctrl` capture;
  talks to the Python service over `echoflow-fcitx.sock` and commits results to
  Fcitx.
- `ui-host/` + `qml/EchoFlowTooltip.qml` (C++/Qt/DTK) — tooltip host over
  `echoflow-ui.sock` plus a system tray icon that opens the DTK settings dialog.
  **Deliberately not PySide/PyQt** (a test enforces their absence from
  `pyproject.toml`).
- Runtime sockets live under `/run/user/$UID/`.

## Testing quirks

`tests/test_install_scripts.py`, `tests/test_model_setup_script.py`, and
`tests/test_ui_host.py` are **spec-as-test**: they read shell scripts,
`ui-host/main.cpp`, `fcitx-addon/echoflow.cpp`, `CMakeLists.txt`, and
`pyproject.toml` as text and assert specific substrings appear (or don't).
Editing those files can break tests even when runtime behavior is fine — check
the asserted strings before refactoring. `test_model_setup_script.py` also
asserts the hard-coded local path
`/home/hualet/projects/hualet/echoflow/model` does **not** appear in the setup
scripts; keep model paths external (`$HOME/AI/Model/...`).

`tests/test_llama_runtime_build.py` is the same kind of spec-as-test for the
llama.cpp submodule, the `llama-runtime/` wrapper CMakeLists, and the
`install-user.sh` llama-build integration.

`test_service.py` / `test_asr_runner.py` mock the Qwen engine and recorders, so
the suite runs with no model weights, no PipeWire, and no Fcitx.

## Conventions & gotchas

- Python **>=3.11**, deps via `uv` (see `uv.lock`). `.venv/` is gitignored;
  `run.sh` recreates it on demand.
- Every source file carries an `SPDX-FileCopyrightText: 2026 Hualet Wang` +
  `SPDX-License-Identifier: GPL-3.0-or-later` header (GPL-3.0-or-later overall).
  Add the same header to new files.
- **Do not commit** model weights, llama.cpp builds/shared libs, recordings, or
  `.venv/`. They are gitignored and provisioned by `scripts/*.sh` to
  `$HOME/AI/Model/Qwen3-ASR-GGUF`.
- Config: DTK standard location at `~/.config/echoflow/echoflow.conf`. Paths
  inside use `$HOME` expansion. `install-user.sh` writes a default `.conf` with
  `asr_runner` pointing to the installed venv entrypoint and never overwrites an
  existing `.conf`.
- `llama-runtime/CMakeLists.txt` — thin CMake wrapper that drives the
  `third_party/llama.cpp` submodule (pinned at `dd9280a` / tag `b9106`),
  selects the GGML backend (`ECHOFLOW_LLM_BACKEND`, default AUTO =
  Vulkan-preferred with CPU fallback, requires both Vulkan SDK and the glslc
  shader compiler for the Vulkan path), and installs `libllama*.so*` +
  `libggml*.so*` into `<asr_project_dir>/qwen_asr_gguf/inference/bin/` via
  `cp -a` (preserves loader symlinks). CUDA is not wired into the default
  chain. Use `echoflow-service --self-test` to see if the runtime is in place.
- `model-0.6B` is the primary model dir; `model/` is a fallback when 0.6B is
  absent. Keep both code paths working.

## Code style

- C++: classes `PascalCase` (`EchoFlow`); methods `camelCase` (`handleKeyEvent`);
  members get a trailing underscore (`ctrlHeld_`, `fd_`, `commitSocketPath_`);
  constants are `kCamelCase` (`kHoldThresholdUs`, `kControlSocketName`).
- Python: PEP 8, `snake_case`; config/state via `@dataclass` + enums; type hints
  throughout (`from __future__ import annotations`).
- Every new source file starts with the SPDX header (see Conventions & gotchas).

## Logging

- C++ addon: use `FCITX_INFO` / `FCITX_WARN` / `FCITX_ERROR` (fcitx macros).
- Python: plain `print()` — the service routes through `log()` which prefixes a
  `[timestamp]`; CLI tools print results to stdout and errors to stderr. Do not
  introduce the stdlib `logging` module.

## Git & commits

- Commit only when the user asks. Before committing: `git status --short`, stage
  only intended files; never model weights, recordings, llama.cpp builds, or
  `.venv/` (all gitignored).
- Subject: concise, imperative, describing the actual change (e.g. "Fix … in
  fcitx addon"). Body: explain *why* and *what*, not how. Wrap ~72 chars.
- History uses plain descriptive subjects + bodies — no `feat:`/`fix:` prefix
  scheme required; match the existing style.
