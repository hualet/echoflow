# Qwen-ASR C++ Port — Design Spec

- Date: 2026-06-18
- Status: Approved (design review)
- Components: new `service/` (C++ daemon), `qwen-asr-runtime/`, new top-level
  `CMakeLists.txt`, `third_party/qwen-asr` (submodule), `install-user.sh`,
  `run.sh`, `systemd/`, `scripts/`, `tests/`, `AGENTS.md`, `README.md`

## 1. Goal

Remove the Python and llama.cpp dependencies entirely. Replace the
Qwen3-ASR-GGUF (llama.cpp + onnxruntime) engine with antirez's pure-C
[**qwen-asr**](https://github.com/antirez/qwen-asr) (0.6B/1.7B, safetensors,
BLAS-only). Port the long-running Python daemon (`echoflow/service.py`) to C++.
Unify all native components under a single top-level CMake project so building
and installing is one toolchain:

```
cmake -S . -B build -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build
cmake --install build
ctest --test-dir build
```

No Python at runtime or build time. No llama.cpp. No Vulkan/glslc/onnxruntime.

### 1.1 Confirmed decisions

1. **Standalone C++ daemon** — port `service.py` 1:1 to a new binary
   `echoflow-service`. The 3-process topology (service / fcitx-addon / ui-host)
   and all three Unix sockets are preserved byte-for-byte.
2. **In-process ASR via the C API** — the daemon links `libqwen_asr.a` and calls
   `qwen_load()` once (lazily on first use), keeping the model resident, then
   `qwen_transcribe(ctx, wavPath)` per utterance (the recorder already produces a
   WAV, so no manual WAV→float32 decode is needed). No subprocess per tap, no
   model reload each time.
3. **`third_party/qwen-asr` submodule + CMake wrapper** — mirrors the existing
   `llama.cpp` / `llama-runtime` pattern.
4. **QTest (Qt's testing framework) + bash spec tests** — reuse the existing Qt6
   dependency for unit tests; no new test framework. Bash/grep for spec-as-test.
5. **Unified top-level CMake** — one root `CMakeLists.txt`,
   `add_subdirectory()` for every component.
6. **Optional `prompt` config key** — wires qwen-asr's `--prompt` term-biasing
   through to the engine.

## 2. Architecture (process topology unchanged)

```
fcitx5 ──┬─ libechoflow.so  (fcitx-addon/, ~unchanged C++)
         │     right-Ctrl capture ──cmd──▶ echoflow-control.sock
         │     ◀──"COMMIT\n<text>"── echoflow-fcitx.sock
         │
echoflow-service  (NEW C++ daemon — was Python service.py)
         │     pw-record subprocess · in-process qwen-asr (safetensors)
         │     ──SHOW_TOOLTIP/RECORDING/TRANSCRIBING/IDLE──▶ echoflow-ui.sock
         │
echoflow-ui  (ui-host/, ~unchanged C++/Qt/DTK)
```

The three sockets (`echoflow-control.sock`, `echoflow-fcitx.sock`,
`echoflow-ui.sock`, all under `/run/user/$UID/`) and their wire protocol are
**unchanged**:

- addon → service: `FOCUS [cursor-rect]`, `BLUR`, `CTRL_DOWN`, `TYPED`
- service → addon: `COMMIT\n<text>` (service is the client here, on the fcitx
  socket)
- service → ui: `SHOW_TOOLTIP…`, `HIDE_TOOLTIP`, `RECORDING`, `TRANSCRIBING`,
  `IDLE`
- replies on the control socket: `RECORDING`, `TRANSCRIBING`, `IDLE`,
  `COMMITTED`, `CANCELLED`, `EMPTY`, `TOOLTIP show/hide/suppressed`, …

Because the protocol is frozen, **`fcitx-addon/` source is untouched** and
`ui-host/` source is *almost* untouched — with one necessary exception: the
**settings dialog** (`ui-host/settings-schema.json`, `ui-host/EchoFlowSettings.cpp`)
still describes the old config schema (GGUF `model_dir` default, `asr_runner`,
`asr_project_dir`, no `prompt`). It must be refreshed to the §6 schema (drop
`asr_runner`/`asr_project_dir`, repoint `model_dir` to the safetensors dir, add
`basic.recognition.prompt`). `ui-host/main.cpp`, the QML, and `SettingsDialog`
are unchanged.

### 2.1 Testability: abstract interfaces

`VoiceSession` depends on four abstract interfaces (`IRecorder`, `IAsrEngine`,
`ICommitter`, `IUiNotifier`) rather than concrete classes, exactly as Python's
`RecorderProtocol` / `AsrProtocol` / `CommitterProtocol` / `UiNotifierProtocol`
did. This lets the QTest suite (§8.1) exercise the state machine with fakes —
no pw-record, no model load, no fcitx.

## 3. New component: `service/` (the C++ daemon)

New top-level directory `service/` (sibling of `fcitx-addon/`, `ui-host/`). It
ports the ~632-line `service.py` directly. To keep the logic unit-testable with
QTest without spawning pw-record / loading the model / talking to fcitx, the
**logic is built as a static library** that both the production binary and the
test binaries link against:

```
service/
  CMakeLists.txt
  main.cpp                 # thin entrypoint: CLI parse, load conf, Server::run()
  libechoflow_service.a    # all logic below
    Config.{h,cpp}         # Config struct, load_dtk_conf(), expand_path(), defaults
    VoiceSession.{h,cpp}   # SessionState enum + handle_command(); identical verbs/replies
    Recorder.{h,cpp}       # PipeWireRecorder: pw-record via posix_spawn, SIGINT stop,
                           #   min-length + min-size guard
    AsrEngine.{h,cpp}      # qwen-asr C API wrapper: lazy qwen_load(), resident model,
                           #   qwen_transcribe(ctx, wavPath); language/prompt; strip punctuation
    Committer.{h,cpp}      # FcitxCommitter: SOCK_DGRAM COMMIT\n<text> -> expect "OK"
    UiNotifier.{h,cpp}     # UnixDatagramUiNotifier: fire-and-forget sendto()
    Server.{h,cpp}         # control-socket recvfrom loop
    SelfTest.{h,cpp}       # runtime_checks() / self_test()
    log.h                  # log() helper with [timestamp] prefix (mirrors Python log())
```

### 3.1 Mapping from Python

| Python (`service.py`) | C++ (`service/`) | Notes |
|-----------------------|------------------|-------|
| `Config` dataclass + `default()` | `Config` struct + `Config::default()` | same fields minus removed keys (§6) |
| `load_dtk_conf` / `load_config` | `loadDtkConf()` | configparser → hand-rolled INI reader (DTK's fixed `[section] value=` schema) |
| `expand_path` | `expandPath()` | `$HOME` / `~` expansion, relative-to-conf-dir |
| `VoiceSession.handle_command` | `VoiceSession::handleCommand()` | `SessionState` enum, same verbs & replies |
| `PipeWireRecorder` | `PipeWireRecorder` | `posix_spawn` replaces `subprocess.Popen`; `kill(SIGINT)` to stop |
| `QwenAsrRunner.transcribe` | `AsrEngine::transcribe()` | **new shape**: in-process C API, model resident |
| `FcitxCommitter` | `FcitxCommitter` | identical datagram protocol |
| `UnixDatagramUiNotifier` / `NullUiNotifier` | `UiNotifier` (interface) | |
| `serve()` | `Server::run()` | single-threaded `recvfrom` loop |
| `runtime_checks` / `self_test` | `SelfTest::checks()` / `SelfTest::run()` | checks BLAS + model dir, not llama libs |
| `main()` (`--config/--self-test/--print-default-config/--transcribe-file`) | `main.cpp` | same four CLI modes |

### 3.2 Concurrency

The server stays **single-threaded** to match Python semantics exactly: the
control socket blocks during transcribe, and queued Ctrl/Blur messages are
processed after, exactly as today. (A worker thread for responsive-during-ASR
is a documented future enhancement, explicitly out of scope for this port.)

### 3.3 Logging

A C++ `log()` helper prints `[YYYY-MM-DD HH:MM:SS] <message>` to stdout, matching
the Python `log()`. No `std::cout`-as-logging scattered in the code; the helper
is the single channel. Per AGENTS.md, no `logging`-module equivalent is needed.

### 3.4 Conventions

C++17, GPL-3.0-or-later SPDX header on every new file. PascalCase classes,
camelCase methods, trailing-`_` members, `kCamelCase` constants — matching
`fcitx-addon/` and `ui-host/`.

## 4. qwen-asr integration

### 4.1 Submodule

`.gitmodules` swaps `third_party/llama.cpp` → `third_party/qwen-asr`. Fresh
clones run `git submodule update --init --recursive third_party/qwen-asr`.
qwen-asr is MIT-licensed (GPL-compatible).

### 4.2 `qwen-asr-runtime/CMakeLists.txt` (replaces `llama-runtime/`)

A thin wrapper that builds qwen-asr's sources into a static library. It must
replicate what qwen-asr's `Makefile` does for its `blas` (Linux) target:

- Compile the **full source set** (matching the Makefile's `SRCS`, minus
  `main.c` which is the standalone CLI we don't want): `qwen_asr.c`,
  `qwen_asr_kernels.c`, `qwen_asr_kernels_generic.c`, `qwen_asr_kernels_neon.c`,
  `qwen_asr_kernels_avx.c`, `qwen_asr_audio.c`, `qwen_asr_encoder.c`,
  `qwen_asr_decoder.c`, `qwen_asr_tokenizer.c`, `qwen_asr_safetensors.c` — all
  into **`libqwen_asr.a`**.
- **Compile all kernel files together** (do NOT conditionally pick one): qwen-asr
  compiles `generic` + `neon` + `avx` in the same build, and the per-arch code is
  `#ifdef`-guarded *inside* those files (`qwen_asr_kernels_impl.h` dispatches
  NEON → `__AVX2__ && __FMA__` → generic). AVX-512 is *optional* acceleration
  inside `qwen_asr_kernels_avx.c` (`#if __AVX512F__ && __AVX512BW__` with an AVX2
  `#else`), never required.
- **Portable baseline, not `-march=native`.** Use a CMake cache variable
  `ECHOFLOW_TARGET_ARCH` (default **`x86-64-v3`**) passed as `-march`. v3 defines
  `__AVX2__`+`__FMA__` (so the AVX2+FMA path compiles and runs on any Haswell-era
  / 2013+ CPU) but not `__AVX512*__` (so the AVX-512 branches compile out).
  Override per-machine with `-DECHOFLOW_TARGET_ARCH=native` (max speed on a known
  host) or `x86-64-v2`/`-v4`. The other flags match the Makefile: `-O3
  -ffast-math -DUSE_BLAS -DUSE_OPENBLAS -I/usr/include/openblas`.
- Find BLAS via `find_package(BLAS)` (OpenBLAS on Linux) and link it; also link
  `m` and `pthread` (qwen-asr uses `pthread`).
- Expose `qwen_asr.h` as a public include dir for consumers.

`qwen-asr-runtime/` does **not** build the `qwen_asr` CLI binary — only the
library. (If a CLI is ever wanted for debugging, it can be an optional target.)

### 4.3 New sole native dependency: `libopenblas-dev`

This replaces the entire current toolchain: Vulkan SDK, glslc, onnxruntime,
numpy, gguf, and the in-tree libllama/libggml build. On Debian/deepin:
`sudo apt install libopenblas-dev`.

### 4.4 Memory

Per qwen-asr's docs, the 0.6B model is ~2.7 GiB resident (mmap'd safetensors +
encoder F32 copy + decoder fused matrix). Acceptable for a long-lived systemd
user service. systemd restarts the daemon if it crashes.

## 5. Unified top-level CMake

New root **`CMakeLists.txt`** (`project(EchoFlow) CXX`, C++17):

```cmake
cmake_minimum_required(VERSION 3.16)
project(EchoFlow LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(ECHOFLOW_BUILD_TESTS "Build QTest unit tests" ON)

add_subdirectory(qwen-asr-runtime)
add_subdirectory(service)
add_subdirectory(fcitx-addon)
add_subdirectory(ui-host)
if(ECHOFLOW_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
```

Each sub-project keeps its own `CMakeLists.txt` (installed targets, Qt6/Fcitx5
`find_package` stay local). The root only orchestrates and installs. The whole
tree is built, installed, and tested with the single command block from §1.

## 6. Model format & config

### 6.1 GGUF+ONNX → safetensors

`REQUIRED_MODEL_FILES` becomes the safetensors set qwen-asr expects in a model
directory (qwen-asr auto-detects 0.6B vs 1.7B from the weights). The
`qwen3_asr_encoder_frontend.int4.onnx`, `qwen3_asr_encoder_backend.int4.onnx`,
and `qwen3_asr_llm.q4_k.gguf` files are gone.

**`scripts/setup-qwen-asr-0.6b.sh`** is rewritten to fetch the safetensors model
into `$HOME/AI/Model/qwen3-asr-0.6b/` — either by driving qwen-asr's own
`download_model.sh` or by curl-ing the safetensors directly. Paths stay
**external** to the repo (the AGENTS.md rule "the hard-coded path
`/home/hualet/projects/hualet/echoflow/model` must not appear in setup scripts"
still holds and is re-asserted by the bash spec tests).

The `model-0.6B` primary + `model/` fallback resolution logic is retained, now
applied to safetensors directories.

### 6.2 DTK `.conf` schema

Keys **kept** (identical meaning): `model_name`, `language`,
`strip_trailing_punctuation`, `min_record_seconds`, `rate`, `channels`,
`format`, `asr_timeout_seconds`, `fcitx_commit`, `recordings_dir`, `model_dir`
(now points at the **safetensors** dir).

Keys **removed** (no longer meaningful):
- `advanced.runtime.asr_runner` — no subprocess runner; ASR is an in-process
  library call.
- `advanced.runtime.asr_project_dir` — no `qwen_asr_gguf` package / GGUF project.

Key **added**:
- `basic.recognition.prompt` — optional string passed to qwen-asr's
  `qwen_set_prompt()` for term-biasing (e.g. "Preserve spelling: PostgreSQL,
  CUDA"). Defaults empty.

New default `model_dir`: `$HOME/AI/Model/qwen3-asr-0.6b`. The
`--print-default-config` and `--self-test` output is generated from the C++
`Config` struct.

## 7. Install script, systemd, run.sh

### 7.1 `install-user.sh`

Shrinks dramatically. Removed: the entire `uv venv` + `uv pip install` block,
all per-component `cmake -S … -B …` calls, and the llama-runtime build block
(and its `--no-llama` flag). New body is the single command block from §1, then
the existing fcitx-addon `.conf` sed, systemd unit install, `daemon-reload`, and
enable/`--now`.

The config-default writing drops the embedded **Python heredoc** — replaced by a
plain shell `cat > … <<EOF` here-doc in `install-user.sh` itself, emitting the
§6.2 schema (no `asr_runner`/`asr_project_dir`, with `prompt`). No new template
file or `configure_file` mechanism. Existing `.conf` is never overwritten, as
today.

### 7.2 `systemd/echoflow.service`

`ExecStart` changes from the venv Python entrypoint to the installed C++ binary
`%h/.local/bin/echoflow-service`, and the venv `Environment=PATH=…/.venv/bin:…`
line is removed (no venv exists anymore). `echoflow-ui.service` is unchanged.

### 7.3 `run.sh`

Rewritten to `cmake --build build && exec ./build/service/echoflow-service`. No
`.venv`, no `uv`.

## 8. Testing

### 8.1 QTest unit tests (`tests/`)

`tests/CMakeLists.txt` enables QtTest (Qt6 already required by `ui-host/`). Test
binaries link `libechoflow_service.a` (the daemon logic) + QtTest. Cases port
from `test_service.py` / `test_asr_runner.py`:

- `test_config.cpp` — DTK `.conf` parsing, defaults, `$HOME` expansion.
- `test_voice_session.cpp` — the state machine with mock `IRecorder` /
  `IAsrEngine` / `ICommitter` / `IUiNotifier` interfaces (no pw-record, no model,
  no fcitx). Asserts every verb → reply transition.
- `test_committer.cpp` — the datagram COMMIT protocol against a loopback socket.
- `test_selftest.cpp` — runtime-check predicates on fake filesystem layouts.

Each is a `QObject` with `private slots`; `QTEST_GUILESS_MAIN` entrypoint. Run
via `ctest --test-dir build` (QTest binaries return nonzero on failure; ctest
aggregates).

### 8.2 Bash spec tests (spec-as-test)

`tests/spec/` (invoked by `tests/CMakeLists.txt` as a `ctest` custom command)
holds `grep`-based assertions over `install-user.sh`, the top-level
`CMakeLists.txt`, `service/main.cpp`, `fcitx-addon/echoflow.cpp`,
`scripts/*.sh` — the same "specific substrings must (or must not) appear"
checks the current Python spec tests perform, including:

- no hard-coded `/home/hualet/projects/hualet/echoflow/model` path;
- no PySide/PyQt (trivially true now, still asserted);
- qwen-asr submodule + BLAS wiring present in CMake;
- the socket/protocol verbs still present in the C++ daemon.

`ctest` is the single test entrypoint; AGENTS.md "Commands" is updated to
`cmake --build build && ctest --test-dir build`.

## 9. Removals & migration

**Deleted:** `echoflow/` (all `.py`), `echoflow/asr_runner.py`, `pyproject.toml`,
`uv.lock`, `echoflow.egg-info/`, `llama-runtime/`, the
`third_party/llama.cpp` submodule, all `tests/*.py`, the Python heredoc in
`install-user.sh`.

**Swapped in `.gitmodules`:** `third_party/llama.cpp` → `third_party/qwen-asr`.

**Rewritten:** `AGENTS.md` (architecture boundaries — the `asr_runner` boundary
becomes "`AsrEngine` is the only module that includes `qwen_asr.h`"; commands;
build flow; testing quirks), `README.md`, `scripts/setup-qwen-asr-0.6b.sh`,
`run.sh`, `install-user.sh`, `uninstall-user.sh` (adds
`rm -f "$PREFIX/bin/echoflow-service"` — today it removes `echoflow-ui` and
`libechoflow.so` but not the service, because the service used to live in the
venv; now it is an installed binary), `systemd/echoflow.service`,
`ui-host/settings-schema.json` + `ui-host/EchoFlowSettings.cpp` (drop
`asr_runner`/`asr_project_dir`, repoint `model_dir` to the safetensors dir, add
`basic.recognition.prompt`; see §2).

**Unchanged (source):** `fcitx-addon/*.cpp`, `ui-host/main.cpp`,
`ui-host/SettingsDialog.*`, `qml/*.qml`,
`systemd/echoflow-ui.service`.

## 10. Risks & open questions

- **qwen-asr CMake transcription.** qwen-asr ships only a `Makefile`; the
  wrapper must reproduce its `SRCS` list, compile flags, and links (`openblas`,
  `m`, `pthread`). The build target is a **portable `x86-64-v3` baseline**
  (configurable via `ECHOFLOW_TARGET_ARCH`), not `-march=native`. All kernel
  files compile together; dispatch is compile-time `#ifdef` (NEON → AVX2+FMA →
  generic), and AVX-512 is optional inside the avx file. Mitigation: cross-check
  the wrapper build against a `make blas` build by diffing a known sample's
  transcription; both must agree (speed differs, output should not).
- **Model availability / download path.** qwen-asr's `download_model.sh` is the
  source of truth for the safetensors URLs and directory layout; the rewritten
  `scripts/setup-qwen-asr-0.6b.sh` should defer to it where possible.
- **~2.7 GiB resident memory.** Acceptable for a user service, but worth noting
  in the README. If it ever becomes a problem, a load-on-demand / idle-unload
   strategy is a future option (out of scope here).
- **INI parser.** DTK's `.conf` uses a fixed `[section]` + `value=` schema. A
  small hand-rolled reader is fine; no need to pull a dependency.

## 11. Out of scope

- Streaming/`--stream` transcription (qwen-asr supports it, but echoflow is
  tap-to-talk offline; not needed).
- GPU/MPS/CUDA backends (qwen-asr is CPU+BLAS only; that is the point).
- Packaging (debian/linglong), CI, pre-commit — still none.
- Any change to the fcitx-addon key-capture or ui-host tooltip behavior.
- A worker thread for the daemon's control loop (§3.2).
