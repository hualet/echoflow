# AGENTS.md

Offline voice input method for deepin/Fcitx5. Push-to-talk via long-press `Ctrl`,
recorded with PipeWire, transcribed by local Qwen ASR, committed to the focused
input context through a Fcitx5 addon. Three layers must stay cleanly separated.

## Commands

```bash
# Tests (unittest, NOT pytest — `pytest tests/` is not wired up)
uv run python -m unittest discover -s tests -v

# Single module / case
uv run python -m unittest tests.test_service -v
uv run python -m unittest tests.test_service.MainCliTests.test_transcribe_file_prints_asr_text_without_starting_service

# Run service locally (auto-creates .venv if missing via `uv venv`, then `uv run`)
cp config.example.json config.json
./run.sh --config config.json

# CLI sanity checks (these are the real "lint/typecheck" — there is no CI)
uv run echoflow-service --print-default-config
uv run qwen-asr-transcribe --help
python3 -m py_compile echoflow/*.py tests/*.py
python3 -m json.tool config.example.json
bash -n install-user.sh uninstall-user.sh run.sh scripts/*.sh

# Build the two C++ components (need Qt6 + Fcitx5 dev packages)
cmake -S ui-host   -B build/ui-host   -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build/ui-host
cmake -S fcitx-addon -B build/fcitx-addon -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build/fcitx-addon
```

There is no CI, no pre-commit, and no `.github/`. The "已验证" section in
`README.md` is the canonical pre-merge checklist — mirror its commands before
claiming work is done. See `verification-before-completion` skill.

## Architecture — boundaries you must respect

- `echoflow/service.py` — push-to-talk state machine, recording, commit. The
  long-running daemon. Entry point `echoflow-service` (`echoflow.service:main`).
- `echoflow/asr_runner.py` — the **only** module that knows the Qwen3-ASR-GGUF
  Python API. The service calls it as a **subprocess** (`qwen-asr-transcribe`,
  `echoflow.asr_runner:main`) so model runtime stays isolated. Do not import
  Qwen classes from `service.py`.
- `fcitx-addon/` (C++) — owns input-context events and `Ctrl` capture; talks to
  the Python service over `echoflow-fcitx.sock` and commits results to Fcitx.
- `ui-host/` + `qml/EchoFlowTooltip.qml` (C++/Qt) — tooltip host over
  `echoflow-ui.sock`. **Deliberately not PySide/PyQt** (a test enforces their
  absence from `pyproject.toml`); future settings UI stays C++/Qt/DTK.
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

`test_service.py` / `test_asr_runner.py` mock the Qwen engine and recorders, so
the suite runs with no model weights, no PipeWire, and no Fcitx.

## Conventions & gotchas

- Python **>=3.11**, deps via `uv` (see `uv.lock`). `.venv/` is gitignored;
  `run.sh` recreates it on demand.
- Every source file carries an `SPDX-FileCopyrightText: 2026 HarryLoong` +
  `SPDX-License-Identifier: GPL-3.0-or-later` header (GPL-3.0-or-later overall).
  Add the same header to new files.
- **Do not commit** model weights, llama.cpp builds/shared libs, recordings, or
  `.venv/`. They are gitignored and provisioned by `scripts/*.sh` to
  `$HOME/AI/Model/Qwen3-ASR-GGUF`.
- Config: copy `config.example.json` → `config.json`. Paths inside use `$HOME`
  expansion. `install-user.sh` rewrites `asr_runner` to the installed venv path
  and never overwrites an existing `config.json`.
- ASR runtime needs `libllama*.so*` + `libggml*.so*` placed at
  `<project>/qwen_asr_gguf/inference/bin/`; this is machine/GPU-specific and not
  automated. Use `echoflow-service --self-test` to see what's missing.
- `model-0.6B` is the primary model dir; `model/` is a fallback when 0.6B is
  absent. Keep both code paths working.
