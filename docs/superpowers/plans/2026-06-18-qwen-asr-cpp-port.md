# Qwen-ASR C++ Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove all Python and llama.cpp dependencies; port the voice-input daemon to C++; drive ASR through antirez's pure-C qwen-asr (safetensors, OpenBLAS); unify every native component under one top-level CMake project so build + install is a single toolchain.

**Architecture:** A new standalone C++ daemon `echoflow-service` (porting `echoflow/service.py` 1:1) links `libqwen_asr.a` in-process and keeps the model resident. The 3-process topology (service / fcitx-addon / ui-host) and the three Unix sockets + their wire protocol are unchanged, so `fcitx-addon/` and `ui-host/` source barely moves. A root `CMakeLists.txt` does `add_subdirectory()` for `qwen-asr-runtime`, `service`, `fcitx-addon`, `ui-host`, and `tests`. qwen-asr is consumed as a git submodule + a thin CMake wrapper (mirrors the old llama.cpp pattern).

**Tech Stack:** C++17, CMake ≥ 3.16, Qt6 (Core/Gui/Qml/Quick/Widgets + DTK6; also QtTest for tests), Fcitx5, PipeWire (`pw-record`), OpenBLAS, qwen-asr (pure C, MIT).

**Commit policy (from AGENTS.md):** Do **not** run `git commit` unless the user explicitly asks. Each task ends with a *Commit boundary* showing the suggested message to use only when the user requests a commit.

**Build/test commands (new world):**
- Configure/build/install: `cmake -S . -B build -DCMAKE_INSTALL_PREFIX="$HOME/.local" -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build && cmake --install build`
- Tests: `ctest --test-dir build --output-on-failure`
- Spec tests: `bash tests/spec/run_spec.sh`

**Spec:** `docs/superpowers/specs/2026-06-18-qwen-asr-cpp-port-design.md`

**Python reference:** The daemon ports `echoflow/service.py` (632 lines). Until Task 17 deletes it, tasks cite it by line range, e.g. `service.py:36-65` = the `Config` dataclass. Do not delete `echoflow/` until Task 17.

---

## File structure

| File | Responsibility | Change |
|------|----------------|--------|
| `.gitmodules` | Submodules | Swap `third_party/llama.cpp` → `third_party/qwen-asr` |
| `qwen-asr-runtime/CMakeLists.txt` | Build `libqwen_asr.a` from the submodule | Create (replaces `llama-runtime/`) |
| `CMakeLists.txt` | Top-level orchestrator | Create |
| `service/CMakeLists.txt` | Build `libechoflow_service.a` + `echoflow-service` binary | Create |
| `service/log.h` | `log("[ts] msg")` helper | Create |
| `service/Config.{h,cpp}` | Config struct, DTK `.conf` INI parser, defaults, path expansion | Create (port `service.py:36-65,109-189`) |
| `service/Interfaces.h` | `IRecorder`/`IAsrEngine`/`ICommitter`/`IUiNotifier` | Create |
| `service/VoiceSession.{h,cpp}` | State machine + `handleCommand` | Create (port `service.py:484-574`) |
| `service/Committer.{h,cpp}` | `FcitxCommitter` datagram COMMIT | Create (port `service.py:443-464`) |
| `service/UiNotifier.{h,cpp}` | `UnixDatagramUiNotifier` | Create (port `service.py:472-481`) |
| `service/SelfTest.{h,cpp}` | Runtime checks | Create (port `service.py:219-344`) |
| `service/Recorder.{h,cpp}` | `PipeWireRecorder` via `posix_spawn` | Create (port `service.py:357-409`) |
| `service/AsrEngine.{h,cpp}` | qwen-asr C API wrapper (resident model) | Create (replaces `asr_runner.py`) |
| `service/Server.{h,cpp}` | Control-socket `recvfrom` loop | Create (port `service.py:577-599`) |
| `service/main.cpp` | CLI: `--config/--self-test/--print-default-config/--transcribe-file` | Create (port `service.py:609-628`) |
| `tests/CMakeLists.txt` | QTest binaries + ctest + spec harness | Create |
| `tests/test_config.cpp` … `test_voice_session.cpp` etc. | Unit tests | Create |
| `tests/spec/run_spec.sh` + `tests/spec/*.sh` | Bash spec-as-test | Create (replaces `tests/test_*_scripts.py`) |
| `scripts/setup-qwen-asr-0.6b.sh` | Fetch safetensors model | Rewrite |
| `install-user.sh` | One cmake call, shell-here-doc conf, no uv | Rewrite |
| `uninstall-user.sh` | Also remove `echoflow-service` binary | Modify |
| `systemd/user/echoflow.service` | ExecStart → C++ binary | Modify |
| `run.sh` | `cmake --build` + run binary | Rewrite |
| `fcitx-addon/CMakeLists.txt`, `ui-host/CMakeLists.txt` | Be `add_subdirectory`-friendly | Modify |
| `ui-host/settings-schema.json` | Settings dialog schema | Modify (drop `asr_runner`/`asr_project_dir`, repoint `model_dir`, add `prompt`) |
| `ui-host/EchoFlowSettings.cpp` | Writes default `.conf` from schema | Modify (drop the two removed keys from the default-write list) |
| `AGENTS.md`, `README.md` | Docs | Rewrite |
| `echoflow/`, `pyproject.toml`, `uv.lock`, `llama-runtime/`, `tests/*.py`, `echoflow.egg-info/` | Python + llama artifacts | Delete (Task 17) |

---

## Task 1: Swap the submodule (llama.cpp → qwen-asr)

**Files:**
- Remove: `third_party/llama.cpp` (submodule)
- Create: `third_party/qwen-asr` (submodule)
- Modify: `.gitmodules`

- [ ] **Step 1: Deinit + remove the llama.cpp submodule**

```bash
git submodule deinit -f third_party/llama.cpp
git rm -f third_party/llama.cpp
rm -rf .git/modules/third_party/llama.cpp
```

- [ ] **Step 2: Add qwen-asr as a submodule**

```bash
git submodule add https://github.com/antirez/qwen-asr.git third_party/qwen-asr
git submodule update --init --recursive third_party/qwen-asr
```

- [ ] **Step 3: Verify**

```bash
test -f third_party/qwen-asr/qwen_asr.h && echo OK
test -f third_party/qwen-asr/Makefile && echo OK
git submodule status third_party/qwen-asr
```
Expected: both print `OK`; `git submodule status` shows the qwen-asr commit.

- [ ] **Step 4: Remove the dead `llama-runtime/` wrapper**

```bash
git rm -rf llama-runtime
```

- [ ] **Commit boundary:** `Swap llama.cpp submodule for antirez/qwen-asr`

---

## Task 2: `qwen-asr-runtime/` CMake wrapper → `libqwen_asr.a`

**Files:**
- Create: `qwen-asr-runtime/CMakeLists.txt`

This reproduces qwen-asr's `Makefile` `blas` (Linux) target: compile the full `SRCS` set (minus `main.c`), with `-O3 -ffast-math -DUSE_BLAS -DUSE_OPENBLAS`, link `openblas`/`m`/`pthread`. All kernel files compile together (per-arch code is `#ifdef`-guarded inside them). The build uses a **portable `x86-64-v3` baseline** (`-march`, configurable via `ECHOFLOW_TARGET_ARCH`), NOT `-march=native` — so the binary runs on any Haswell-era (2013+) CPU. v3 defines `__AVX2__`+`__FMA__` (AVX2+FMA path compiles/active) but not `__AVX512*__` (AVX-512 branches compile out). Override per-machine with `-DECHOFLOW_TARGET_ARCH=native`/`x86-64-v2`/`x86-64-v4`.

- [ ] **Step 1: Write the wrapper CMakeLists**

Create `qwen-asr-runtime/CMakeLists.txt`:

```cmake
# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later
# Thin CMake wrapper that builds antirez/qwen-asr into a static library.
# Mirrors the Makefile's `blas` (Linux) target.
cmake_minimum_required(VERSION 3.16)

set(QWEN_ASR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/qwen-asr"
    CACHE PATH "qwen-asr source directory")

set(QWEN_ASR_SOURCES
    "${QWEN_ASR_DIR}/qwen_asr.c"
    "${QWEN_ASR_DIR}/qwen_asr_kernels.c"
    "${QWEN_ASR_DIR}/qwen_asr_kernels_generic.c"
    "${QWEN_ASR_DIR}/qwen_asr_kernels_neon.c"
    "${QWEN_ASR_DIR}/qwen_asr_kernels_avx.c"
    "${QWEN_ASR_DIR}/qwen_asr_audio.c"
    "${QWEN_ASR_DIR}/qwen_asr_encoder.c"
    "${QWEN_ASR_DIR}/qwen_asr_decoder.c"
    "${QWEN_ASR_DIR}/qwen_asr_tokenizer.c"
    "${QWEN_ASR_DIR}/qwen_asr_safetensors.c")

add_library(qwen_asr STATIC ${QWEN_ASR_SOURCES})
target_include_directories(qwen_asr PUBLIC "${QWEN_ASR_DIR}")

# Portable baseline: x86-64-v3 (AVX2+FMA) by default, NOT -march=native.
# Override per-machine: -DECHOFLOW_TARGET_ARCH=native (or x86-64-v2 / -v4).
# v3 defines __AVX2__+__FMA__ (AVX2 path active) but not __AVX512*__ (AVX-512
# branches, which are optional inside qwen_asr_kernels_avx.c, compile out).
set(ECHOFLOW_TARGET_ARCH "x86-64-v3" CACHE STRING "GCC -march baseline for qwen-asr kernels")
target_compile_options(qwen_asr PRIVATE
    -O3 -march=${ECHOFLOW_TARGET_ARCH} -ffast-math
    -DUSE_BLAS -DUSE_OPENBLAS)

find_package(BLAS REQUIRED)            # OpenBLAS on Linux
target_link_libraries(qwen_asr PUBLIC ${BLAS_LIBRARIES} m pthread)

# The .c files exist only in the submodule; tell CMake they're generated elsewhere.
target_sources(qwen_asr PRIVATE FILE_SET HEADERS BASE_DIR "${QWEN_ASR_DIR}"
    FILES "${QWEN_ASR_DIR}/qwen_asr.h")
```

Note: `qwen_asr.h` is the only header consumers need; it is the public include.

- [ ] **Step 2: Build it standalone to verify**

```bash
cmake -S qwen-asr-runtime -B build/qwen-asr-runtime -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/qwen-asr-runtime
test -f build/qwen-asr-runtime/libqwen_asr.a && echo OK
```
Expected: `OK`. If `find_package(BLAS)` fails, install `sudo apt install libopenblas-dev`.

- [ ] **Step 3: Cross-check against qwen-asr's own `make blas` (optional but recommended)**

In a scratch dir, build qwen-asr natively and transcribe a sample; later (Task 11) compare the same sample through the CMake-built library. Both must agree. (No assertion here — just note the reference output.)

- [ ] **Commit boundary:** `Add qwen-asr-runtime CMake wrapper building libqwen_asr.a`

---

## Task 3: Top-level `CMakeLists.txt` skeleton

**Files:**
- Create: `CMakeLists.txt` (repo root)

For now it only orchestrates `qwen-asr-runtime`; later tasks add the other subdirs.

- [ ] **Step 1: Write the root CMakeLists**

```cmake
# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.16)
project(EchoFlow LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(ECHOFLOW_BUILD_TESTS "Build QTest unit tests + spec tests" ON)

add_subdirectory(qwen-asr-runtime)
# add_subdirectory(service)        # Task 11
# add_subdirectory(fcitx-addon)    # Task 12
# add_subdirectory(ui-host)        # Task 12
if(ECHOFLOW_BUILD_TESTS)
    enable_testing()
    # add_subdirectory(tests)      # Task 16
endif()
```

- [ ] **Step 2: Verify the single-command build (library only so far)**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
test -f build/qwen-asr-runtime/libqwen_asr.a && echo OK
```
Expected: `OK`.

- [ ] **Commit boundary:** `Add top-level CMakeLists orchestrating qwen-asr-runtime`

---

## Task 4: `service/` skeleton + `Config` (TDD)

**Files:**
- Create: `service/log.h`, `service/Config.h`, `service/Config.cpp`, `service/CMakeLists.txt`
- Test: `tests/test_config.cpp` (+ `tests/CMakeLists.txt` minimal, expanded in Task 16)

`Config` ports `service.py:36-65` (struct + `default()`), `109-113` (`expand_path`), `116-189` (`_DTK_PATH_TO_CONFIG_KEY`, `_convert_conf_value`, `load_dtk_conf`). It must use the *new* schema (no `asr_runner`/`asr_project_dir`; add `prompt`).

- [ ] **Step 1: Write `service/log.h`**

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ECHOFLOW_LOG_H
#define ECHOFLOW_LOG_H
#include <cstdio>
#include <ctime>
#include <string>
namespace echoflow {
inline void log(const std::string& message) {
    std::time_t t = std::time(nullptr);
    char ts[20];
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    std::printf("[%s] %s\n", ts, message.c_str());
    std::fflush(stdout);
}
}  // namespace echoflow
#endif
```

- [ ] **Step 2: Write `service/Config.h`**

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ECHOFLOW_CONFIG_H
#define ECHOFLOW_CONFIG_H
#include <filesystem>
#include <string>
#include <optional>
namespace echoflow {

struct PipeWireRecordConfig {
    int rate = 16000;
    int channels = 1;
    std::string format = "s16";
};

struct Config {
    std::string recordingsDir;
    std::string asrProjectDir;        // kept in struct for self-test paths; not in .conf
    std::string modelDir;
    std::string modelName = "qwen-asr-0.6b";
    std::optional<std::string> language = "Chinese";
    std::string prompt;               // NEW (optional term-biasing)
    int asrTimeoutSeconds = 120;
    double minRecordSeconds = 0.25;
    PipeWireRecordConfig pwRecord;
    bool fcitxCommit = true;
    bool stripTrailingPunctuation = false;

    static Config defaultConfig();
};

// Parse the DTK INI .conf ([section]\nvalue=... ). baseDir = parent of the conf file.
Config loadDtkConf(const std::filesystem::path& path);
// Expand $HOME / ~ and resolve relative to baseDir.
std::string expandPath(const std::string& value, const std::filesystem::path& baseDir);
std::filesystem::path runtimeDir();
std::filesystem::path controlSocketPath(const Config& cfg);
std::filesystem::path fcitxSocketPath(const Config& cfg);
std::filesystem::path uiSocketPath(const Config& cfg);
std::string stripPunctuation(const std::string& text);

}  // namespace echoflow
#endif
```

- [ ] **Step 3: Write the failing test `tests/test_config.cpp`**

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include "Config.h"
using namespace echoflow;
class TestConfig : public QObject { Q_OBJECT
private slots:
    void defaultConfigHasExpectedFields();
    void expandPathResolvesHome();
    void loadDtkConfParsesValues();
    void loadDtkConfIgnoresUnknownSections();
};

void TestConfig::defaultConfigHasExpectedFields() {
    Config c = Config::defaultConfig();
    QCOMPARE(c.modelName, std::string("qwen-asr-0.6b"));
    QCOMPARE(c.pwRecord.rate, 16000);
    QCOMPARE(c.pwRecord.format, std::string("s16"));
    QCOMPARE(c.fcitxCommit, true);
    QVERIFY(c.language.has_value());
    QCOMPARE(*c.language, std::string("Chinese"));
}

void TestConfig::expandPathResolvesHome() {
    setenv("HOME", "/tmp/fakehome", 1);
    std::filesystem::path base("/home/u/.config/echoflow");
    QCOMPARE(expandPath("$HOME/AI/Model/qwen3-asr-0.6b", base),
             std::string("/tmp/fakehome/AI/Model/qwen3-asr-0.6b"));
    // relative path resolves against the conf's parent dir
    QCOMPARE(expandPath("recordings", base),
             std::string("/home/u/.config/echoflow/recordings"));
}

void TestConfig::loadDtkConfParsesValues() {
    QTemporaryFile f;
    QVERIFY(f.open());
    // DTK schema: [section] then value=
    f.write("[basic.model.model_name]\nvalue=qwen-asr-0.6b\n"
            "[basic.recognition.language]\nvalue=English\n"
            "[basic.recognition.prompt]\nvalue=Preserve spelling: CUDA\n"
            "[basic.recording.rate]\nvalue=22050\n"
            "[basic.recording.min_record_seconds]\nvalue=0.5\n"
            "[basic.recognition.strip_trailing_punctuation]\nvalue=true\n"
            "[advanced.fcitx.fcitx_commit]\nvalue=false\n"
            "[advanced.runtime.model_dir]\nvalue=$HOME/AI/Model/qwen3-asr-0.6b\n");
    f.close();
    Config c = loadDtkConf(f.fileName().toStdString());
    QCOMPARE(c.modelName, std::string("qwen-asr-0.6b"));
    QCOMPARE(*c.language, std::string("English"));
    QCOMPARE(c.prompt, std::string("Preserve spelling: CUDA"));
    QCOMPARE(c.pwRecord.rate, 22050);
    QCOMPARE(c.minRecordSeconds, 0.5);
    QCOMPARE(c.stripTrailingPunctuation, true);
    QCOMPARE(c.fcitxCommit, false);
    QVERIFY(c.modelDir.find("qwen3-asr-0.6b") != std::string::npos);
}

void TestConfig::loadDtkConfIgnoresUnknownSections() {
    QTemporaryFile f;
    QVERIFY(f.open());
    f.write("[some.unknown.thing]\nvalue=ignored\n[basic.model.model_name]\nvalue=qwen-asr-0.6b\n");
    f.close();
    Config c = loadDtkConf(f.fileName().toStdString());
    QCOMPARE(c.modelName, std::string("qwen-asr-0.6b"));
}
```

- [ ] **Step 4: Minimal `tests/CMakeLists.txt` (expanded later)**

```cmake
find_package(Qt6 REQUIRED COMPONENTS Test)
qt_standard_project_setup()
add_executable(test_config test_config.cpp
    ${CMAKE_SOURCE_DIR}/service/Config.cpp)
target_include_directories(test_config PRIVATE ${CMAKE_SOURCE_DIR}/service)
target_link_libraries(test_config PRIVATE Qt6::Test)
add_test(NAME test_config COMMAND test_config)
```

- [ ] **Step 5: Run the test to verify it fails (link error — `Config.cpp` missing)**

```bash
cmake -S . -B build && cmake --build build --target test_config 2>&1 | tail -5
```
Expected: build failure (undefined `Config::defaultConfig()` etc.).

- [ ] **Step 6: Implement `service/Config.cpp`**

Port `service.py:99-189`. Key pieces (write the full file):

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Config.h"
#include "log.h"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <sys/types.h>

namespace echoflow {
namespace fs = std::filesystem;

Config Config::defaultConfig() {
    const char* home = std::getenv("HOME");
    std::string h = home ? home : "/tmp";
    Config c;
    c.recordingsDir = h + "/.local/share/echoflow/recordings";
    c.modelDir = h + "/AI/Model/qwen3-asr-0.6b";
    c.asrProjectDir = h + "/AI/Model";   // informational only
    return c;
}

std::string expandPath(const std::string& value, const fs::path& baseDir) {
    std::string s = value;
    // expand $HOME
    const char* home = std::getenv("HOME");
    if (home) {
        std::string token = "$HOME";
        for (size_t pos = 0; (pos = s.find(token, pos)) != std::string::npos; pos += std::string(home).size())
            s.replace(pos, token.size(), home);
    }
    if (!s.empty() && s[0] == '~') s.replace(0, 1, std::string(home ? home : "/"));
    fs::path p(s);
    if (!p.is_absolute()) p = baseDir / p;
    return p.string();
}

fs::path runtimeDir() {
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg && access(xdg, W_OK | X_OK) == 0) return xdg;
    fs::path ru = fs::path("/run/user") / std::to_string(getuid());
    if (fs::exists(ru)) return ru;
    return "/tmp";
}
fs::path controlSocketPath(const Config&) { return runtimeDir() / "echoflow-control.sock"; }
fs::path fcitxSocketPath(const Config&)   { return runtimeDir() / "echoflow-fcitx.sock"; }
fs::path uiSocketPath(const Config&)      { return runtimeDir() / "echoflow-ui.sock"; }

std::string stripPunctuation(const std::string& text) {
    static const char* kTrail = "。．.，,、！？!?；;：:\n\r\t ";
    auto end = text.find_last_not_of(kTrail);
    return end == std::string::npos ? std::string() : text.substr(0, end + 1);
}

// --- DTK .conf parsing (port of service.py:116-189) ---
namespace {
struct Map { const char* section; const char* field; };  // field is a key into Config
// We map section -> a setter by string compare below.
}

Config loadDtkConf(const fs::path& path) {
    Config cfg = Config::defaultConfig();
    std::ifstream in(path);
    if (!in) return cfg;
    std::string line, section;
    while (std::getline(in, line)) {
        auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos) continue;
        if (line[first] == '[') {
            auto close = line.find(']', first);
            section = (close != std::string::npos)
                ? line.substr(first + 1, close - first - 1) : std::string();
            continue;
        }
        if (section.empty()) continue;
        // only "value=..." lines matter
        std::string key, val;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        key = line.substr(first, eq - first);
        auto kb = key.find_first_not_of(" \t"), ke = key.find_last_not_of(" \t");
        key = (kb != std::string::npos) ? key.substr(kb, ke - kb + 1) : std::string();
        val = line.substr(eq + 1);
        if (key != "value") continue;
        auto vb = val.find_first_not_of(" \t");
        val = (vb != std::string::npos) ? val.substr(vb) : std::string();
        // strip trailing CR/whitespace
        auto ve = val.find_last_not_of(" \t\r");
        if (ve != std::string::npos) val.erase(ve + 1);

        // Apply by section name.
        auto boolval = [](const std::string& v) {
            std::string l; for (char c : v) l += (char)tolower(c);
            return l == "true" || l == "1" || l == "yes" || l == "on"; };

        if (section == "basic.model.model_name") cfg.modelName = val;
        else if (section == "basic.recognition.language") cfg.language = val.empty() ? std::optional<std::string>{} : std::optional<std::string>(val);
        else if (section == "basic.recognition.prompt") cfg.prompt = val;
        else if (section == "basic.recognition.strip_trailing_punctuation") cfg.stripTrailingPunctuation = boolval(val);
        else if (section == "basic.recording.min_record_seconds") cfg.minRecordSeconds = std::stod(val);
        else if (section == "basic.recording.rate") cfg.pwRecord.rate = std::stoi(val);
        else if (section == "basic.recording.channels") cfg.pwRecord.channels = std::stoi(val);
        else if (section == "basic.recording.format") cfg.pwRecord.format = val;
        else if (section == "advanced.runtime.model_dir") cfg.modelDir = val;
        else if (section == "advanced.runtime.asr_timeout_seconds") cfg.asrTimeoutSeconds = std::stoi(val);
        else if (section == "advanced.fcitx.fcitx_commit") cfg.fcitxCommit = boolval(val);
        else if (section == "advanced.storage.recordings_dir") cfg.recordingsDir = val;
        // unknown sections (e.g. legacy asr_runner/asr_project_dir) are ignored.
    }
    fs::path base = path.parent_path();
    for (auto* s : { &cfg.recordingsDir, &cfg.modelDir })
        if (!s->empty()) *s = expandPath(*s, base);
    return cfg;
}

}  // namespace echoflow
```

- [ ] **Step 7: Add `service/CMakeLists.txt` (library target, used by tests now)**

```cmake
# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later
add_library(echoflow_service STATIC
    Config.cpp)
target_include_directories(echoflow_service PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
```

Update root `CMakeLists.txt`: uncomment `add_subdirectory(service)` and `add_subdirectory(tests)`.

- [ ] **Step 8: Run the test to verify it passes**

```bash
cmake --build build --target test_config
ctest --test-dir build -R test_config --output-on-failure
```
Expected: 4 assertions pass.

- [ ] **Commit boundary:** `Port Config + DTK .conf parser to C++ (TDD)`

---

## Task 5: Interfaces + `VoiceSession` state machine (TDD)

**Files:**
- Create: `service/Interfaces.h`, `service/VoiceSession.h`, `service/VoiceSession.cpp`
- Test: `tests/test_voice_session.cpp`

Ports `service.py:484-574`. `VoiceSession` depends on four abstract interfaces so tests inject fakes (mirrors the Python `Protocol` mocks).

- [ ] **Step 1: Write `service/Interfaces.h`**

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ECHOFLOW_INTERFACES_H
#define ECHOFLOW_INTERFACES_H
#include <filesystem>
#include <memory>
#include <string>
namespace echoflow {

struct AudioResult {
    std::filesystem::path path;     // empty => cancelled/too short
};

class IRecorder {
public:
    virtual ~IRecorder() = default;
    virtual void start() = 0;
    virtual std::filesystem::path stop() = 0;   // empty path == cancelled
};

class IAsrEngine {
public:
    virtual ~IAsrEngine() = default;
    virtual std::string transcribe(const std::filesystem::path& audio) = 0;
};

class ICommitter {
public:
    virtual ~ICommitter() = default;
    // returns {ok, detail}
    virtual std::pair<bool, std::string> commitText(const std::string& text) = 0;
};

class IUiNotifier {
public:
    virtual ~IUiNotifier() = default;
    virtual void send(const std::string& message) = 0;
};

}  // namespace echoflow
#endif
```

- [ ] **Step 2: Write `service/VoiceSession.h`**

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ECHOFLOW_VOICE_SESSION_H
#define ECHOFLOW_VOICE_SESSION_H
#include "Config.h"
#include "Interfaces.h"
namespace echoflow {

enum class SessionState { Idle, Recording, Transcribing };

class VoiceSession {
public:
    VoiceSession(Config cfg, IRecorder& recorder, IAsrEngine& asr,
                 ICommitter& committer, IUiNotifier& ui);
    // Returns the reply line (without trailing newline). Fakes the Python handle_command.
    std::string handleCommand(const std::string& command);
    SessionState state() const { return state_; }
private:
    std::string startRecording();
    std::string stopTranscribeCommit();
    Config cfg_;
    IRecorder& recorder_;
    IAsrEngine& asr_;
    ICommitter& committer_;
    IUiNotifier& ui_;
    SessionState state_ = SessionState::Idle;
    bool tooltipVisible_ = false;
    bool typedHidden_ = false;
};

}  // namespace echoflow
#endif
```

- [ ] **Step 3: Write the failing test `tests/test_voice_session.cpp`**

Port the transitions from `tests/test_service.py` (FOCUS/BLUR/CTRL_DOWN/TYPED). Use fake implementations of each interface that record calls.

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include "VoiceSession.h"
using namespace echoflow;

struct FakeRecorder : IRecorder {
    int starts = 0; std::filesystem::path returnPath = "/tmp/x.wav";
    void start() override { ++starts; }
    std::filesystem::path stop() override { return returnPath; }
};
struct FakeAsr : IAsrEngine {
    std::string result = "hello"; int calls = 0;
    std::string transcribe(const std::filesystem::path&) override { ++calls; return result; }
};
struct FakeCommitter : ICommitter {
    std::string last; std::pair<bool,std::string> ret = {true,"OK"};
    std::pair<bool,std::string> commitText(const std::string& t) override { last = t; return ret; }
};
struct FakeUi : IUiNotifier {
    std::vector<std::string> msgs;
    void send(const std::string& m) override { msgs.push_back(m); }
};

class TestVoiceSession : public QObject { Q_OBJECT
private slots:
    void focusShowsTooltip();
    void ctrlDownWhenIdleStartsRecording();
    void ctrlDownTwiceCommits();
    void blurDuringRecordingStopsAndIdles();
    void typedHidesTooltipWhenIdle();
    void typedIgnoredWhileRecording();
    void unknownCommand();
};

static VoiceSession makeSession(FakeRecorder& r, FakeAsr& a, FakeCommitter& c, FakeUi& u) {
    return VoiceSession(Config::defaultConfig(), r, a, c, u);
}

void TestVoiceSession::focusShowsTooltip() {
    FakeRecorder r; FakeAsr a; FakeCommitter c; FakeUi u;
    auto s = makeSession(r,a,c,u);
    QCOMPARE(s.handleCommand("FOCUS"), std::string("TOOLTIP show"));
    QVERIFY(!u.msgs.empty());
    QVERIFY(u.msgs.back().find("SHOW_TOOLTIP") == 0);
}
void TestVoiceSession::ctrlDownWhenIdleStartsRecording() {
    FakeRecorder r; FakeAsr a; FakeCommitter c; FakeUi u;
    auto s = makeSession(r,a,c,u);
    QCOMPARE(s.handleCommand("CTRL_DOWN"), std::string("RECORDING"));
    QCOMPARE(r.starts, 1);
}
void TestVoiceSession::ctrlDownTwiceCommits() {
    FakeRecorder r; FakeAsr a; FakeCommitter c; FakeUi u;
    auto s = makeSession(r,a,c,u);
    s.handleCommand("CTRL_DOWN");                       // start
    QCOMPARE(s.handleCommand("CTRL_DOWN"), std::string("COMMITTED"));
    QCOMPARE(a.calls, 1);
    QCOMPARE(c.last, std::string("hello"));
}
void TestVoiceSession::blurDuringRecordingStopsAndIdles() {
    FakeRecorder r; FakeAsr a; FakeCommitter c; FakeUi u;
    auto s = makeSession(r,a,c,u);
    s.handleCommand("CTRL_DOWN");
    QCOMPARE(s.handleCommand("BLUR"), std::string("TOOLTIP hide"));
    QCOMPARE(s.state(), SessionState::Idle);
}
void TestVoiceSession::typedHidesTooltipWhenIdle() {
    FakeRecorder r; FakeAsr a; FakeCommitter c; FakeUi u;
    auto s = makeSession(r,a,c,u);
    s.handleCommand("FOCUS");
    QCOMPARE(s.handleCommand("TYPED"), std::string("TYPING hide"));
    // subsequent FOCUS chatter does not re-show while suppressed
    QCOMPARE(s.handleCommand("FOCUS"), std::string("TOOLTIP suppressed"));
}
void TestVoiceSession::typedIgnoredWhileRecording() {
    FakeRecorder r; FakeAsr a; FakeCommitter c; FakeUi u;
    auto s = makeSession(r,a,c,u);
    s.handleCommand("CTRL_DOWN");
    QCOMPARE(s.handleCommand("TYPED"), std::string("IGNORED"));
}
void TestVoiceSession::unknownCommand() {
    FakeRecorder r; FakeAsr a; FakeCommitter c; FakeUi u;
    auto s = makeSession(r,a,c,u);
    QCOMPARE(s.handleCommand("FROBNICATE"), std::string("ERR unknown-command"));
}
```

- [ ] **Step 4: Add `VoiceSession.cpp` to `service/CMakeLists.txt` and add the test to `tests/CMakeLists.txt`**

Append `VoiceSession.cpp` to the `echoflow_service` source list. In `tests/CMakeLists.txt` add:
```cmake
add_executable(test_voice_session test_voice_session.cpp)
target_link_libraries(test_voice_session PRIVATE echoflow_service Qt6::Test)
add_test(NAME test_voice_session COMMAND test_voice_session)
```

- [ ] **Step 5: Run, verify it fails to link (no `VoiceSession.cpp` body)**

```bash
cmake --build build --target test_voice_session 2>&1 | tail -3
```

- [ ] **Step 6: Implement `service/VoiceSession.cpp`**

Direct port of `service.py:503-563` (`handle_command` + `_start_recording` + `_stop_transcribe_commit`):

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later
#include "VoiceSession.h"
#include "log.h"
#include <algorithm>
#include <cctype>
namespace echoflow {

VoiceSession::VoiceSession(Config cfg, IRecorder& recorder, IAsrEngine& asr,
                           ICommitter& committer, IUiNotifier& ui)
    : cfg_(std::move(cfg)), recorder_(recorder), asr_(asr), committer_(committer), ui_(ui) {}

static std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::toupper(c); });
    return s;
}
static std::string trim(std::string s) {
    auto a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? std::string() : s.substr(a, b - a + 1);
}

std::string VoiceSession::handleCommand(const std::string& command) {
    std::string cmd = trim(command);
    auto sp = cmd.find(' ');
    std::string verb = upper(sp == std::string::npos ? cmd : cmd.substr(0, sp));
    std::string arg = trim(sp == std::string::npos ? std::string() : cmd.substr(sp + 1));

    if (verb == "FOCUS") {
        tooltipVisible_ = true;
        if (typedHidden_) return "TOOLTIP suppressed";
        std::string msg = "SHOW_TOOLTIP" + (arg.empty() ? std::string() : (" " + arg)) + " 按右 Ctrl 语音输入";
        ui_.send(msg);
        return "TOOLTIP show";
    }
    if (verb == "BLUR") {
        tooltipVisible_ = false;
        typedHidden_ = false;
        if (state_ == SessionState::Recording) recorder_.stop();
        state_ = SessionState::Idle;
        ui_.send("HIDE_TOOLTIP");
        return "TOOLTIP hide";
    }
    if (verb == "TYPED") {
        if (state_ == SessionState::Idle && tooltipVisible_ && !typedHidden_) {
            typedHidden_ = true;
            ui_.send("HIDE_TOOLTIP");
            return "TYPING hide";
        }
        return "IGNORED";
    }
    if (verb == "CTRL_DOWN") {
        if (state_ == SessionState::Idle)   return startRecording();
        if (state_ == SessionState::Recording) return stopTranscribeCommit();
        return "TRANSCRIBING";
    }
    return "ERR unknown-command";
}

std::string VoiceSession::startRecording() {
    recorder_.start();
    ui_.send("RECORDING");
    state_ = SessionState::Recording;
    return "RECORDING";
}

std::string VoiceSession::stopTranscribeCommit() {
    state_ = SessionState::Transcribing;
    ui_.send("TRANSCRIBING");
    auto audio = recorder_.stop();
    if (audio.empty()) { state_ = SessionState::Idle; ui_.send("IDLE"); return "CANCELLED"; }
    std::string text = asr_.transcribe(audio);
    if (cfg_.stripTrailingPunctuation) text = stripPunctuation(text);
    if (text.empty()) { state_ = SessionState::Idle; ui_.send("IDLE"); return "EMPTY"; }
    auto [ok, detail] = committer_.commitText(text);
    state_ = SessionState::Idle;
    ui_.send("IDLE");
    return ok ? "COMMITTED" : ("ERR " + detail);
}

}  // namespace echoflow
```

- [ ] **Step 7: Run, verify pass**

```bash
cmake --build build --target test_voice_session
ctest --test-dir build -R test_voice_session --output-on-failure
```
Expected: all 7 cases pass.

- [ ] **Commit boundary:** `Port VoiceSession state machine to C++ (TDD)`

---

## Task 6: `Committer` (TDD)

**Files:**
- Create: `service/Committer.h`, `service/Committer.cpp`
- Test: `tests/test_committer.cpp`

Ports `service.py:443-464`: bind a transient client datagram socket, send `COMMIT\n<text>` to the fcitx server socket, expect `OK`.

- [ ] **Step 1: Write the failing test `tests/test_committer.cpp`**

Stand up a real loopback datagram socket as the "fcitx server", point `fcitxSocketPath` at it via a temp runtime dir, and assert the round trip.

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <thread>
#include "Committer.h"
using namespace echoflow;

class TestCommitter : public QObject { Q_OBJECT
private slots:
    void commitReturnsOkOnAck();
    void commitReturnsErrOnNack();
private:
    std::string srv_;
    int setupServer() {
        int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
        srv_ = std::string("/tmp/echoflow-test-") + std::to_string(getpid()) + ".sock";
        unlink(srv_.c_str());
        struct sockaddr_un addr{}; addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, srv_.c_str(), sizeof(addr.sun_path)-1);
        ::bind(fd, (struct sockaddr*)&addr, sizeof(addr));
        return fd;
    }
};

// Receives one datagram (capturing the peer address) and sends a reply back to
// THAT peer. recv()+send() on an unconnected SOCK_DGRAM loses the peer and the
// reply never reaches the transient client socket — must use recvfrom/sendto.
// No QTest asserts here: this runs on a worker thread; if recvfrom fails the
// Committer times out (0.5s) and QVERIFY(ok) fails on the main thread instead.
static void recvAndReply(int fd, const std::string& reply) {
    char buf[512]; struct sockaddr_un peer{}; socklen_t plen = sizeof(peer);
    ssize_t n = recvfrom(fd, buf, sizeof(buf)-1, 0, (struct sockaddr*)&peer, &plen);
    if (n > 0) sendto(fd, reply.data(), reply.size(), 0, (struct sockaddr*)&peer, plen);
}

void TestCommitter::commitReturnsOkOnAck() {
    int s = setupServer();
    Config cfg; Committer comm(cfg, srv_);   // ctor takes explicit socket path for testability
    std::thread t([s]{ recvAndReply(s, "OK\n"); });
    auto [ok, detail] = comm.commitText("hello");
    QVERIFY(ok);
    t.join(); close(s); unlink(srv_.c_str());
}

void TestCommitter::commitReturnsErrOnNack() {
    int s = setupServer();
    Config cfg; Committer comm(cfg, srv_);
    std::thread t([s]{ recvAndReply(s, "NO\n"); });
    auto [ok, detail] = comm.commitText("hi");
    QVERIFY(!ok);
    t.join(); close(s); unlink(srv_.c_str());
}
```

> Note: `Committer`'s constructor takes an explicit `fcitxSocket` path so tests don't depend on the global runtime-dir resolution. The production wiring (Task 10) passes `fcitxSocketPath(cfg)`. The reply path uses `recvfrom`+`sendto` because the server socket is **not** `connect()`ed — `send()` on an unconnected datagram socket fails with `ENOTCONN`.

- [ ] **Step 2: Write `service/Committer.h`**

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ECHOFLOW_COMMITTER_H
#define ECHOFLOW_COMMITTER_H
#include "Config.h"
#include "Interfaces.h"
namespace echoflow {
class Committer : public ICommitter {
public:
    Committer(const Config& cfg, std::filesystem::path fcitxSocket);
    std::pair<bool, std::string> commitText(const std::string& text) override;
private:
    const Config& cfg_;
    std::filesystem::path fcitxSocket_;
};
}  // namespace echoflow
#endif
```

- [ ] **Step 3: Implement `service/Committer.cpp` (port `service.py:447-464`)**

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Committer.h"
#include "log.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <chrono>
namespace echoflow {

Committer::Committer(const Config& cfg, std::filesystem::path fcitxSocket)
    : cfg_(cfg), fcitxSocket_(std::move(fcitxSocket)) {}

std::pair<bool,std::string> Committer::commitText(const std::string& text) {
    if (!cfg_.fcitxCommit) return {false, "fcitx disabled"};
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) return {false, "socket"};
    struct timeval tv{0, 500000};  // 0.5s
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    // bind a transient client path
    struct sockaddr_un caddr{}; caddr.sun_family = AF_UNIX;
    std::string client = "/tmp/echoflow-client-" + std::to_string(getpid()) + "-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".sock";
    unlink(client.c_str());
    strncpy(caddr.sun_path, client.c_str(), sizeof(caddr.sun_path)-1);
    if (bind(fd, (struct sockaddr*)&caddr, sizeof(caddr)) < 0) { close(fd); unlink(client.c_str()); return {false,"bind"}; }
    struct sockaddr_un saddr{}; saddr.sun_family = AF_UNIX;
    strncpy(saddr.sun_path, fcitxSocket_.c_str(), sizeof(saddr.sun_path)-1);
    std::string payload = "COMMIT\n" + text;
    if (sendto(fd, payload.data(), payload.size(), 0, (struct sockaddr*)&saddr, sizeof(saddr)) < 0) {
        close(fd); unlink(client.c_str()); return {false, "sendto"};
    }
    char buf[256] = {0};
    ssize_t n = recv(fd, buf, sizeof(buf)-1, 0);
    close(fd); unlink(client.c_str());
    if (n <= 0) return {false, "timeout"};
    std::string reply(buf, n);
    auto e = reply.find_last_not_of(" \t\r\n");
    reply = (e == std::string::npos) ? std::string() : reply.substr(0, e+1);
    return { reply == "OK", reply };
}
}  // namespace echoflow
```

- [ ] **Step 4: Wire into `service/CMakeLists.txt`, add test, run**

Append `Committer.cpp`. In `tests/CMakeLists.txt` add the `test_committer` target linking `echoflow_service Qt6::Test pthread`. Run:
```bash
cmake --build build --target test_committer
ctest --test-dir build -R test_committer --output-on-failure
```
Expected: both cases pass.

- [ ] **Commit boundary:** `Port FcitxCommitter to C++ (TDD)`

---

## Task 7: `UiNotifier` + `SelfTest` (TDD)

**Files:**
- Create: `service/UiNotifier.h/.cpp`, `service/SelfTest.h/.cpp`
- Test: `tests/test_selftest.cpp`

### 7a. `UiNotifier` (port `service.py:472-481`)

- [ ] **Step 1: Write `service/UiNotifier.h`**

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ECHOFLOW_UI_NOTIFIER_H
#define ECHOFLOW_UI_NOTIFIER_H
#include "Interfaces.h"
#include <filesystem>
namespace echoflow {
class UnixDatagramUiNotifier : public IUiNotifier {
public:
    explicit UnixDatagramUiNotifier(std::filesystem::path socket) : socket_(std::move(socket)) {}
    void send(const std::string& message) override;
private:
    std::filesystem::path socket_;
};
}  // namespace echoflow
#endif
```

- [ ] **Step 2: Implement** (`sendto`, ignore errors except log):

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later
#include "UiNotifier.h"
#include "log.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
namespace echoflow {
void UnixDatagramUiNotifier::send(const std::string& message) {
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) return;
    struct sockaddr_un addr{}; addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_.c_str(), sizeof(addr.sun_path)-1);
    sendto(fd, message.data(), message.size(), 0, (struct sockaddr*)&addr, sizeof(addr));
    close(fd);
}
}  // namespace echoflow
```

### 7b. `SelfTest` (port `service.py:219-344`)

The C++ self-test drops llama-lib checks and adds a qwen-asr model-dir check. It exposes pure predicate functions so they're unit-testable on fake filesystem layouts.

- [ ] **Step 3: Write `service/SelfTest.h`**

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ECHOFLOW_SELFTEST_H
#define ECHOFLOW_SELFTEST_H
#include "Config.h"
#include <filesystem>
#include <vector>
namespace echoflow {
struct RuntimeCheck { std::string name; bool passed; std::string detail; };

// Exact files qwen-asr expects in a 0.6B model dir (from its download_model.sh).
// For 1.7B the dir also holds model.safetensors.index.json + 2 shards; we treat
// "has model.safetensors OR an index+shard set" as present.
inline constexpr const char* const kRequiredModelFiles[] = {
    "config.json", "generation_config.json", "model.safetensors",
    "vocab.json", "merges.txt"
};

// Pure predicates (testable without real resources):
std::vector<std::filesystem::path> modelDirCandidates(const std::filesystem::path& modelDir);
std::filesystem::path resolveModelDir(const Config& cfg);
bool canCreateDirectory(const std::filesystem::path& path);
std::vector<std::string> missingModelFiles(const std::filesystem::path& modelDir);

std::vector<RuntimeCheck> runtimeChecks(const Config& cfg);
int runSelfTest(const Config& cfg);   // prints [OK]/[FAIL] lines, returns 0/1
}  // namespace echoflow
#endif
```

- [ ] **Step 4: Write the failing test `tests/test_selftest.cpp`**

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <fstream>
#include "SelfTest.h"
using namespace echoflow;
class TestSelfTest : public QObject { Q_OBJECT
private slots:
    void modelDirCandidatesHasFallback();
    void canCreateDirectoryOnTmp();
    void missingModelFilesListsAbsent();
};
void TestSelfTest::modelDirCandidatesHasFallback() {
    auto v = modelDirCandidates("/x/model-0.6B");
    QVERIFY(std::find(v.begin(), v.end(), std::filesystem::path("/x/model")) != v.end());
    auto v2 = modelDirCandidates("/x/model");
    QVERIFY(std::find(v2.begin(), v2.end(), std::filesystem::path("/x/model-0.6B")) != v2.end());
}
void TestSelfTest::canCreateDirectoryOnTmp() {
    QTemporaryDir d;
    QVERIFY(canCreateDirectory(std::filesystem::path(d.path().toStdString()) / "sub/deep"));
}
void TestSelfTest::missingModelFilesListsAbsent() {
    QTemporaryDir d;
    auto dir = std::filesystem::path(d.path().toStdString());
    std::ofstream(dir / "config.json").put('x');   // only one required file present
    auto miss = missingModelFiles(dir);
    QVERIFY(miss.size() == 4);                      // the other 4 are missing
    QVERIFY(std::find(miss.begin(), miss.end(), std::string("model.safetensors")) != miss.end());
}
```

- [ ] **Step 5: Implement `service/SelfTest.cpp`** (port `service.py:219-247,272-344`; the llama-lib check is dropped since `libqwen_asr.a` is statically linked, but the model check is strengthened to verify the exact safetensors file set — see `kRequiredModelFiles` and `missingModelFiles`):

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SelfTest.h"
#include "log.h"
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
namespace echoflow {
namespace fs = std::filesystem;

std::vector<fs::path> modelDirCandidates(const fs::path& modelDir) {
    if (modelDir.filename() == "model-0.6B") return { modelDir, modelDir.parent_path()/"model" };
    if (modelDir.filename() == "model")      return { modelDir, modelDir.parent_path()/"model-0.6B" };
    return { modelDir };
}
fs::path resolveModelDir(const Config& cfg) {
    for (auto& c : modelDirCandidates(fs::path(cfg.modelDir)))
        if (fs::exists(c)) return c;
    return cfg.modelDir;
}
bool canCreateDirectory(const fs::path& path) {
    fs::path p = path;
    while (!fs::exists(p) && p != p.parent_path()) p = p.parent_path();
    struct stat st;
    if (stat(p.c_str(), &st) != 0) return false;
    return access(p.c_str(), W_OK | X_OK) == 0;
}

std::vector<std::string> missingModelFiles(const fs::path& modelDir) {
    std::vector<std::string> miss;
    if (!fs::exists(modelDir)) {
        for (auto* f : kRequiredModelFiles) miss.emplace_back(f);
        return miss;
    }
    for (auto* f : kRequiredModelFiles)
        if (!fs::exists(modelDir / f)) miss.emplace_back(f);
    // 1.7B ships shards instead of a single model.safetensors; if the index is
    // present, treat "model.safetensors" as satisfied even when the single file
    // is absent (shards cover it).
    if (!miss.empty() && fs::exists(modelDir / "model.safetensors.index.json")) {
        miss.erase(std::remove(miss.begin(), miss.end(), std::string("model.safetensors")), miss.end());
    }
    return miss;
}

std::vector<RuntimeCheck> runtimeChecks(const Config& cfg) {
    auto modelDir = resolveModelDir(cfg);
    auto missing = missingModelFiles(modelDir);
    bool modelOk = missing.empty();
    std::string modelDetail = modelOk ? modelDir.string()
        : (modelDir.string() + " missing: " + ([&]{ std::string s; for (size_t i=0;i<missing.size();++i){ s += missing[i]; if (i+1<missing.size()) s += ", "; } return s; })());
    std::vector<RuntimeCheck> checks = {
        {"recordings dir can be created", canCreateDirectory(cfg.recordingsDir), cfg.recordingsDir},
        {"pw-record available", std::system("command -v pw-record >/dev/null 2>&1") == 0, "pw-record"},
        {"model dir exists", fs::exists(modelDir), modelDir.string()},
        {"model files present", modelOk, modelDetail},
        {"control socket path parent", fs::exists(controlSocketPath(cfg).parent_path()), controlSocketPath(cfg).string()},
        {"fcitx socket path parent",   fs::exists(fcitxSocketPath(cfg).parent_path()),   fcitxSocketPath(cfg).string()},
        {"ui socket path parent",      fs::exists(uiSocketPath(cfg).parent_path()),      uiSocketPath(cfg).string()},
    };
    return checks;
}
int runSelfTest(const Config& cfg) {
    bool ok = true;
    for (auto& c : runtimeChecks(cfg)) {
        std::printf("[%s] %s: %s\n", c.passed?"OK":"FAIL", c.name.c_str(), c.detail.c_str());
        ok = ok && c.passed;
    }
    return ok ? 0 : 1;
}
}  // namespace echoflow
```

- [ ] **Step 6: Wire + run**

Add `UiNotifier.cpp SelfTest.cpp` to `service/CMakeLists.txt`; add `test_selftest` target. `cmake --build build && ctest --test-dir build -R "test_selftest|test_ui" --output-on-failure`. Expected: pass.

- [ ] **Commit boundary:** `Port UiNotifier + SelfTest to C++ (TDD)`

---

## Task 8: `Recorder` (PipeWire via `posix_spawn`)

**Files:**
- Create: `service/Recorder.h`, `service/Recorder.cpp`

Ports `service.py:357-409`. Not cleanly unit-testable (needs `pw-record`); verified by a manual smoke test in Task 11.

- [ ] **Step 1: Write `service/Recorder.h`**

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ECHOFLOW_RECORDER_H
#define ECHOFLOW_RECORDER_H
#include "Config.h"
#include "Interfaces.h"
#include <chrono>
#include <filesystem>
#include <sys/types.h>   // pid_t
namespace echoflow {
class PipeWireRecorder : public IRecorder {
public:
    explicit PipeWireRecorder(Config cfg);
    void start() override;
    std::filesystem::path stop() override;
private:
    Config cfg_;
    pid_t child_ = -1;
    std::filesystem::path path_;
    std::chrono::steady_clock::time_point startedAt_;
};
}  // namespace echoflow
#endif
```

- [ ] **Step 2: Implement `service/Recorder.cpp`** (port `service.py:365-409`; `posix_spawn` + `kill(SIGINT)` + min-length/size guard):

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Recorder.h"
#include "log.h"
#include <spawn.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <ctime>
#include <cstring>
#include <fstream>
extern char** environ;

namespace echoflow {
namespace fs = std::filesystem;

PipeWireRecorder::PipeWireRecorder(Config cfg) : cfg_(std::move(cfg)) {}

static std::string stamp() {
    std::time_t t = std::time(nullptr); char b[32];
    std::strftime(b, sizeof(b), "%Y%m%d-%H%M%S", std::localtime(&t));
    return b;
}

void PipeWireRecorder::start() {
    if (child_ != -1) return;
    fs::create_directories(cfg_.recordingsDir);
    path_ = fs::path(cfg_.recordingsDir) / ("voice-" + stamp() + ".wav");

    std::string rate = std::to_string(cfg_.pwRecord.rate);
    std::string ch   = std::to_string(cfg_.pwRecord.channels);
    std::vector<std::string> argv = {"pw-record","--rate",rate,"--channels",ch,"--format",cfg_.pwRecord.format,path_.string()};
    std::vector<char*> cargv;
    for (auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
    cargv.push_back(nullptr);
    pid_t pid;
    if (posix_spawnp(&pid, "pw-record", nullptr, nullptr, cargv.data(), environ) != 0) {
        log("posix_spawnp pw-record failed"); child_ = -1; path_.clear(); return;
    }
    child_ = pid;
    startedAt_ = std::chrono::steady_clock::now();
    log("recording started: " + path_.string());
}

std::filesystem::path PipeWireRecorder::stop() {
    if (child_ == -1) return {};
    double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt_).count();
    pid_t pid = child_; auto path = path_; child_ = -1; path_.clear();
    kill(pid, SIGINT);
    int status = 0;
    for (int i = 0; i < 50; ++i) {          // wait up to ~5s
        if (waitpid(pid, &status, WNOHANG) != 0) break;
        usleep(100000);
    }
    if (waitpid(pid, &status, WNOHANG) == 0) { kill(pid, SIGTERM); waitpid(pid, &status, 0); }
    if (elapsed < cfg_.minRecordSeconds) { log("recording too short"); return {}; }
    std::error_code ec;
    auto sz = fs::file_size(path, ec);
    if (ec || sz < 1024) { log("recording missing or too small"); return {}; }
    return path;
}
}  // namespace echoflow
```

- [ ] **Step 3: Wire into `service/CMakeLists.txt`** (append `Recorder.cpp`); build the lib:
```bash
cmake --build build --target echoflow_service
```
Expected: builds cleanly.

- [ ] **Commit boundary:** `Port PipeWireRecorder to C++ (posix_spawn)`

---

## Task 9: `AsrEngine` (qwen-asr C API wrapper)

**Files:**
- Create: `service/AsrEngine.h`, `service/AsrEngine.cpp`

This is the only module that `#include "qwen_asr.h"`. It loads the model lazily on first `transcribe()` and keeps `ctx_` resident. Uses `qwen_transcribe(ctx, wavPath)` (the recorder already produces a WAV).

- [ ] **Step 1: Write `service/AsrEngine.h`**

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ECHOFLOW_ASR_ENGINE_H
#define ECHOFLOW_ASR_ENGINE_H
#include "Interfaces.h"
#include "Config.h"
struct qwen_ctx_t;
namespace echoflow {
class AsrEngine : public IAsrEngine {
public:
    explicit AsrEngine(Config cfg);
    ~AsrEngine();
    std::string transcribe(const std::filesystem::path& audio) override;
private:
    void ensureLoaded();
    Config cfg_;
    qwen_ctx_t* ctx_ = nullptr;
};
}  // namespace echoflow
#endif
```

- [ ] **Step 2: Implement `service/AsrEngine.cpp`**

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later
#include "AsrEngine.h"
#include "log.h"
#include "qwen_asr.h"   // third_party/qwen-asr
#include <cstdlib>
namespace echoflow {

AsrEngine::AsrEngine(Config cfg) : cfg_(std::move(cfg)) {}
AsrEngine::~AsrEngine() { if (ctx_) qwen_free(ctx_); }

void AsrEngine::ensureLoaded() {
    if (ctx_) return;
    // resolve model dir (model-0.6B primary, model/ fallback)
    fs::path model = resolveModelDir(cfg_);
    log("loading qwen-asr model: " + model.string());
    ctx_ = qwen_load(model.c_str());
    if (!ctx_) { log("qwen_load failed"); return; }
    if (cfg_.language) qwen_set_force_language(ctx_, cfg_.language->c_str());
    if (!cfg_.prompt.empty()) qwen_set_prompt(ctx_, cfg_.prompt.c_str());
}

std::string AsrEngine::transcribe(const std::filesystem::path& audio) {
    ensureLoaded();
    if (!ctx_) return "";
    char* out = qwen_transcribe(ctx_, audio.string().c_str());
    std::string text = out ? std::string(out) : std::string();
    if (out) std::free(out);
    return text;
}
}  // namespace echoflow
```

> Behavior note: qwen-asr uses greedy decode (no `temperature` param in the C API), whereas the Python runner used `temperature=0.4`. This is a deliberate, documented simplification; greedy is the qwen-asr default and is fine for short voice commands.

- [ ] **Step 3: Make `echoflow_service` link `qwen_asr`**

In `service/CMakeLists.txt` add: `target_link_libraries(echoflow_service PUBLIC qwen_asr)` and `target_sources(echoflow_service PRIVATE AsrEngine.cpp)`.

- [ ] **Step 4: Build (requires a downloaded model to *run*, but only compiles now)**

```bash
cmake --build build --target echoflow_service
```
Expected: links against `libqwen_asr.a` + OpenBLAS cleanly.

- [ ] **Commit boundary:** `Add AsrEngine wrapping qwen-asr C API (resident model)`

---

## Task 10: `Server` + `main.cpp` → `echoflow-service` binary

**Files:**
- Create: `service/Server.h`, `service/Server.cpp`, `service/main.cpp`
- Modify: `service/CMakeLists.txt`, root `CMakeLists.txt`

### 10a. `Server` (port `service.py:569-599`)

- [ ] **Step 1: Write `service/Server.h`**

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ECHOFLOW_SERVER_H
#define ECHOFLOW_SERVER_H
#include "Config.h"
#include "VoiceSession.h"
namespace echoflow {
class Server {
public:
    Server(Config cfg, VoiceSession& session);
    int run();   // blocks; binds control socket, recvfrom loop
private:
    Config cfg_;
    VoiceSession& session_;
};
}  // namespace echoflow
#endif
```

- [ ] **Step 2: Implement `service/Server.cpp`** (port `service.py:569-599`):

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Server.h"
#include "log.h"
#include <sys/socket.h>
#include <sys/stat.h>   // chmod
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <array>
namespace echoflow {
namespace fs = std::filesystem;
static const std::array<std::string,4> kAllowed = {"FOCUS","BLUR","CTRL_DOWN","TYPED"};
Server::Server(Config cfg, VoiceSession& s) : cfg_(std::move(cfg)), session_(s) {}
int Server::run() {
    auto srv = controlSocketPath(cfg_);
    fs::create_directories(srv.parent_path());
    ::unlink(srv.c_str());
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) { log("socket failed"); return 1; }
    struct sockaddr_un addr{}; addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, srv.c_str(), sizeof(addr.sun_path)-1);
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { log("bind failed"); close(fd); return 1; }
    chmod(srv.c_str(), 0600);
    log("EchoFlow service listening on " + srv.string());
    char buf[4096];
    for (;;) {
        struct sockaddr_un peer{}; socklen_t plen = sizeof(peer);
        ssize_t n = recvfrom(fd, buf, sizeof(buf)-1, 0, (struct sockaddr*)&peer, &plen);
        if (n <= 0) continue;
        buf[n] = 0;
        std::string cmd(buf);
        std::string verb = cmd.substr(0, cmd.find(' '));
        bool ok = false; for (auto& a : kAllowed) if (verb == a) { ok = true; break; }
        std::string reply = ok ? (session_.handleCommand(cmd) + "\n") : "ERR unknown-command\n";
        if (plen > 0) sendto(fd, reply.data(), reply.size(), 0, (struct sockaddr*)&peer, plen);
    }
}
}  // namespace echoflow
```

### 10b. `main.cpp` (port `service.py:602-628`)

- [ ] **Step 3: Write `service/main.cpp`**

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Config.h"
#include "VoiceSession.h"
#include "Recorder.h"
#include "AsrEngine.h"
#include "Committer.h"
#include "UiNotifier.h"
#include "Server.h"
#include "SelfTest.h"
#include "log.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
namespace fs = std::filesystem;
namespace { std::filesystem::path defaultConfigPath() {
    if (const char* x = std::getenv("XDG_CONFIG_HOME")) return fs::path(x)/"echoflow"/"echoflow.conf";
    return fs::path(std::getenv("HOME")) / ".config/echoflow/echoflow.conf";
}}

int main(int argc, char** argv) {
    std::string cfgPath = defaultConfigPath().string();
    bool selfTest=false, printDefault=false; std::string transcribeFile;
    for (int i=1;i<argc;++i) {
        std::string a=argv[i];
        if (a=="--config" && i+1<argc) cfgPath=argv[++i];
        else if (a=="--self-test") selfTest=true;
        else if (a=="--print-default-config") printDefault=true;
        else if (a=="--transcribe-file" && i+1<argc) transcribeFile=argv[++i];
        else if (a=="--help") {
            std::printf("Usage: echoflow-service [--config PATH] [--self-test] [--print-default-config] [--transcribe-file FILE]\n");
            return 0;
        }
    }
    if (printDefault) {
        auto c = echoflow::Config::defaultConfig();
        std::printf("{\n  \"model_dir\": \"%s\",\n  \"model_name\": \"%s\",\n  \"language\": \"%s\",\n  \"prompt\": \"%s\",\n  \"recordings_dir\": \"%s\",\n  \"min_record_seconds\": %.2f,\n  \"rate\": %d,\n  \"fcitx_commit\": %s\n}\n",
            c.modelDir.c_str(), c.modelName.c_str(), c.language.value_or("").c_str(), c.prompt.c_str(),
            c.recordingsDir.c_str(), c.minRecordSeconds, c.pwRecord.rate, c.fcitxCommit?"true":"false");
        return 0;
    }
    echoflow::Config cfg = fs::exists(cfgPath) ? echoflow::loadDtkConf(cfgPath) : echoflow::Config::defaultConfig();
    if (selfTest) return echoflow::runSelfTest(cfg);
    if (!transcribeFile.empty()) {
        echoflow::AsrEngine asr(cfg);
        std::string text = asr.transcribe(transcribeFile);
        if (cfg.stripTrailingPunctuation) text = echoflow::stripPunctuation(text);
        std::printf("%s\n", text.c_str());
        return 0;
    }
    echoflow::PipeWireRecorder rec(cfg);
    echoflow::AsrEngine asr(cfg);
    echoflow::Committer comm(cfg, echoflow::fcitxSocketPath(cfg));
    echoflow::UnixDatagramUiNotifier ui(echoflow::uiSocketPath(cfg));
    echoflow::VoiceSession session(cfg, rec, asr, comm, ui);
    echoflow::Server server(cfg, session);
    return server.run();
}
```

### 10c. Build the binary

- [ ] **Step 4: Add the binary target in `service/CMakeLists.txt`** and uncomment `add_subdirectory(service)` in root:

```cmake
add_executable(echoflow-service main.cpp Server.cpp)
target_link_libraries(echoflow-service PRIVATE echoflow_service qwen_asr)
install(TARGETS echoflow-service RUNTIME DESTINATION bin)
```
(Also add `Server.cpp` to the `echoflow_service` lib sources so tests can reuse it if desired, or keep it binary-only — either is fine.)

- [ ] **Step 5: Build + smoke-test the CLI (no model needed for these)**

```bash
cmake --build build --target echoflow-service
./build/service/echoflow-service --print-default-config
./build/service/echoflow-service --self-test
```
Expected: `--print-default-config` prints JSON; `--self-test` prints `[OK]/[FAIL]` lines.

- [ ] **Step 6: End-to-end smoke (needs a model + pw-record + fcitx running)**

With `echoflow-ui` and fcitx running, start the service and tap right-Ctrl twice; confirm transcription commits. (Manual; documented as the integration check.)

- [ ] **Commit boundary:** `Add echoflow-service daemon binary (Server + main)`

---

## Task 11: Bring `fcitx-addon` + `ui-host` under the top-level CMake; refresh ui-host settings

**Files:**
- Modify: `fcitx-addon/CMakeLists.txt`, `ui-host/CMakeLists.txt`, root `CMakeLists.txt`
- Modify: `ui-host/settings-schema.json`, `ui-host/EchoFlowSettings.cpp`

The CMake changes make the two existing sub-projects `add_subdirectory`-safe. The
settings refresh is required because the schema still describes the old config
(GGUF `model_dir` default, `asr_runner`, `asr_project_dir`, no `prompt`) — without
it the settings dialog writes stale keys it can no longer edit, and the new
`prompt` / safetensors `model_dir` are unreachable from the UI.

- [ ] **Step 1: Make each subdir guard against re-declaring project**

In each of `fcitx-addon/CMakeLists.txt` and `ui-host/CMakeLists.txt`, change the top `project(...)` line to:
```cmake
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # built as part of the top-level project
else()
    cmake_minimum_required(VERSION 3.16)
    project(<name> LANGUAGES CXX)
endif()
```
Keep all their existing `find_package(Qt6 …)`, `find_package(Fcitx5 …)`, targets, and `install()` rules unchanged.

- [ ] **Step 2: Add both to the root `CMakeLists.txt`**

Uncomment:
```cmake
add_subdirectory(fcitx-addon)
add_subdirectory(ui-host)
```

- [ ] **Step 3: Full single-command build**

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX="$HOME/.local" -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```
Expected: builds `libqwen_asr.a`, `libechoflow_service.a`, `echoflow-service`, `libechoflow.so`, `echoflow-ui` in one pass.

### 11b. Refresh the ui-host settings schema (config keys changed)

- [ ] **Step 4: Update `ui-host/settings-schema.json`**

Three edits to the `advanced.runtime` group:
1. **Delete** the `asr_project_dir` option (lines ~78-82) and the `asr_runner` option (lines ~90-94) entirely.
2. **Repoint** the `model_dir` option default from `$HOME/AI/Model/Qwen3-ASR-GGUF/model-0.6B` to the safetensors dir, and fix the label:

```json
{
  "key": "model_dir",
  "name": "模型权重目录（safetensors）",
  "type": "lineedit",
  "default": "$HOME/AI/Model/qwen3-asr-0.6b"
}
```

3. **Add** a `prompt` option to the `basic.recognition` group (after `strip_trailing_punctuation`):

```json
{
  "key": "prompt",
  "name": "识别提示词（可选，用于术语纠正）",
  "type": "lineedit",
  "default": ""
}
```

- [ ] **Step 5: Update `ui-host/EchoFlowSettings.cpp`**

In `init()` (the `paths` string list ~L45-59), delete the two lines:
```cpp
            QStringLiteral("advanced.runtime.asr_project_dir"),
            QStringLiteral("advanced.runtime.asr_runner"),
```
and add:
```cpp
            QStringLiteral("basic.recognition.prompt"),
```
The `prompt` key is auto-written to the default `.conf` because the list iterates schema options by path; `populateComboBoxes()` is unaffected.

- [ ] **Step 6: Rebuild + verify the settings dialog emits the new schema**

```bash
cmake --build build --target echoflow-ui
# smoke: delete a test conf, run the UI once to regenerate defaults, inspect
rm -f /tmp/ef-test.conf
QT_QPA_PLATFORM=offscreen ./build/ui-host/echoflow-ui --config /tmp/ef-test.conf &
sleep 1; kill %1 2>/dev/null
grep -E 'asr_runner|asr_project_dir|prompt|model_dir' /tmp/ef-test.conf
```
Expected: `prompt` and `model_dir` present; **no** `asr_runner`/`asr_project_dir` lines.

> Note: if `echoflow-ui` has no `--config` flag, drive the regeneration via `EchoFlowSettings::init(path)` in a tiny throwaway `main`, or just assert via the spec test in Task 14 instead. Either way, the schema change is the source of truth.

- [ ] **Commit boundary:** `Unify fcitx-addon + ui-host under top-level CMake; refresh settings schema`

---

## Task 12: Model provisioning script (safetensors)

**Files:**
- Rewrite: `scripts/setup-qwen-asr-0.6b.sh`

- [ ] **Step 1: Rewrite the script** to fetch the 0.6B safetensors into `$HOME/AI/Model/qwen3-asr-0.6b/`, deferring to qwen-asr's own `download_model.sh` (whose signature is `--model small|large --dir DIR`; the directory must exist before invocation):

```bash
#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
DEST="${DEST:-$HOME/AI/Model/qwen3-asr-0.6b}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
QWEN_ASR="$ROOT_DIR/third_party/qwen-asr"

if [[ ! -d "$QWEN_ASR" ]]; then
  echo "qwen-asr submodule missing; run: git submodule update --init --recursive third_party/qwen-asr" >&2
  exit 1
fi
mkdir -p "$DEST"
# download_model.sh --dir writes the safetensors straight into $DEST (no temp/mv).
bash "$QWEN_ASR/download_model.sh" --model small --dir "$DEST"
echo "Model installed at $DEST"
```

- [ ] **Step 2: Verify the download** (network required):

```bash
bash scripts/setup-qwen-asr-0.6b.sh
test -d "$HOME/AI/Model/qwen3-asr-0.6b" && echo OK
```

- [ ] **Step 3: Verify the model transcribes** through the CMake-built library:

```bash
./build/service/echoflow-service --transcribe-file third_party/qwen-asr/samples/jfk.wav
```
Expected: prints a transcript. (Compare against `make blas` output from Task 2 Step 3 — must agree.)

- [ ] **Commit boundary:** `Rewrite model setup script for qwen-asr safetensors`

---

## Task 13: Install script, systemd, run.sh, uninstall

**Files:**
- Rewrite: `install-user.sh`, `run.sh`
- Modify: `uninstall-user.sh`, `systemd/user/echoflow.service`

- [ ] **Step 1: Rewrite `install-user.sh`** — one cmake call, shell-here-doc conf (new schema), no uv, no llama:

```bash
#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${PREFIX:-$HOME/.local}"
STATE_DIR="${STATE_DIR:-$HOME/.local/share/echoflow}"
CONFIG_DIR="${CONFIG_DIR:-$HOME/.config/echoflow}"
SYSTEMD_USER_DIR="${SYSTEMD_USER_DIR:-$HOME/.config/systemd/user}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build/install-user}"
BUILD_TYPE="${CMAKE_BUILD_TYPE:-RelWithDebInfo}"
START_SERVICES=1
usage() { cat <<EOF
Usage: $0 [--no-start]
  --no-start  Install and enable user services without starting them.
  -h, --help  Show this help.
EOF
}
while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-start) START_SERVICES=0 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
  shift
done
mkdir -p "$STATE_DIR" "$CONFIG_DIR" "$SYSTEMD_USER_DIR"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build "$BUILD_DIR"
cmake --install "$BUILD_DIR"

if [[ ! -e "$CONFIG_DIR/echoflow.conf" ]]; then
  cat > "$CONFIG_DIR/echoflow.conf" <<EOF
[basic.model.model_name]
value=qwen-asr-0.6b
[basic.recognition.language]
value=Chinese
[basic.recognition.prompt]
value=
[basic.recognition.strip_trailing_punctuation]
value=false
[basic.recording.min_record_seconds]
value=0.25
[basic.recording.rate]
value=16000
[basic.recording.channels]
value=1
[basic.recording.format]
value=s16
[advanced.runtime.model_dir]
value=\$HOME/AI/Model/qwen3-asr-0.6b
[advanced.runtime.asr_timeout_seconds]
value=120
[advanced.fcitx.fcitx_commit]
value=true
[advanced.storage.recordings_dir]
value=\$HOME/.local/share/echoflow/recordings
EOF
fi

ADDON_LIB="$PREFIX/lib/fcitx5/libechoflow.so"
if [[ ! -e "$ADDON_LIB" ]]; then ADDON_LIB="$(find "$PREFIX" -path '*/fcitx5/libechoflow.so' -print -quit)"; fi
[[ -e "$ADDON_LIB" ]] || { echo "Could not find libechoflow.so under $PREFIX" >&2; exit 1; }
FCITX_ADDON_DIR="$HOME/.local/share/fcitx5/addon"
mkdir -p "$FCITX_ADDON_DIR"
sed "s|^Library=.*|Library=${ADDON_LIB%.so}|" "$ROOT_DIR/fcitx-addon/echoflow.conf.in" > "$FCITX_ADDON_DIR/echoflow.conf"

install -m 0644 "$ROOT_DIR/systemd/user/echoflow.service" "$SYSTEMD_USER_DIR/echoflow.service"
install -m 0644 "$ROOT_DIR/systemd/user/echoflow-ui.service" "$SYSTEMD_USER_DIR/echoflow-ui.service"
systemctl --user daemon-reload
if [[ "$START_SERVICES" == "1" ]]; then
  systemctl --user enable --now echoflow.service echoflow-ui.service
else
  systemctl --user enable echoflow.service echoflow-ui.service
fi
echo "EchoFlow installed."
```

- [ ] **Step 2: Update `systemd/user/echoflow.service`** ExecStart + drop venv PATH:

```ini
# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later
[Unit]
Description=EchoFlow offline voice input service
After=graphical-session.target

[Service]
Type=simple
WorkingDirectory=%h/.local/share/echoflow
ExecStart=%h/.local/bin/echoflow-service
Restart=on-failure

[Install]
WantedBy=default.target
```

- [ ] **Step 3: Rewrite `run.sh`**:
```sh
#!/bin/sh
# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
cmake --build build 2>/dev/null || cmake -S . -B build && cmake --build build
exec ./build/service/echoflow-service "$@"
```

- [ ] **Step 4: Modify `uninstall-user.sh`** — add the service binary removal (insert after the `echoflow-ui` line):

```bash
rm -f "$PREFIX/bin/echoflow-service"
```

- [ ] **Step 5: Verify end-to-end install + uninstall on a clean prefix**

```bash
./install-user.sh --no-start
test -x "$HOME/.local/bin/echoflow-service" && echo OK
./uninstall-user.sh
test ! -e "$HOME/.local/bin/echoflow-service" && echo "uninstalled OK"
```

- [ ] **Commit boundary:** `Rewrite install/run/uninstall for single-toolchain C++ build`

---

## Task 14: QTest harness + bash spec tests

**Files:**
- Create: `tests/CMakeLists.txt` (final form), `tests/spec/run_spec.sh`, `tests/spec/*.sh`

- [ ] **Step 1: Finalize `tests/CMakeLists.txt`** — enumerate all QTest binaries + the spec script as a ctest case:

```cmake
# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later
find_package(Qt6 REQUIRED COMPONENTS Test)
qt_standard_project_setup()

set(ECHOFLOW_TESTS test_config test_voice_session test_committer test_selftest)
foreach(t ${ECHOFLOW_TESTS})
    add_executable(${t} ${t}.cpp)
    target_link_libraries(${t} PRIVATE echoflow_service Qt6::Test pthread)
    add_test(NAME ${t} COMMAND ${t})
endforeach()

add_test(NAME spec_tests
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/spec/run_spec.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
```

- [ ] **Step 2: Write `tests/spec/run_spec.sh`** (replaces `tests/test_*_scripts.py`). Each `assert_contains`/`assert_absent` mirrors the Python spec tests:

```bash
#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
pass=0; fail=0
assert_contains() {  # file needle [description]
  if grep -qF "$2" "$1"; then echo "ok   - $3"; pass=$((pass+1))
  else echo "FAIL - $3 ($1 missing: $2)"; fail=$((fail+1)); fi
}
assert_absent() {  # file needle [description]
  if grep -qF "$2" "$1"; then echo "FAIL - $3 ($1 should not contain: $2)"; fail=$((fail+1))
  else echo "ok   - $3"; pass=$((pass+1)); fi
}

# No Python anywhere in the build/install path
assert_absent "$ROOT/install-user.sh" "uv venv"        "install-user.sh has no uv venv"
assert_absent "$ROOT/install-user.sh" "uv pip"         "install-user.sh has no uv pip"
assert_absent "$ROOT/install-user.sh" "llama"          "install-user.sh has no llama"
assert_absent "$ROOT/install-user.sh" "asr_runner"     "no asr_runner key"
assert_absent "$ROOT/install-user.sh" "asr_project_dir" "no asr_project_dir key"
# Single cmake invocation
assert_contains "$ROOT/install-user.sh" 'cmake -S "$ROOT_DIR"'  "one configure step"
assert_contains "$ROOT/install-user.sh" 'cmake --install'        "one install step"
# Submodule + BLAS wiring
assert_contains "$ROOT/CMakeLists.txt" "qwen-asr-runtime"        "top-level builds qwen-asr-runtime"
assert_contains "$ROOT/qwen-asr-runtime/CMakeLists.txt" "USE_OPENBLAS" "BLAS defines present"
# qwen-asr is the only ASR; daemon includes its header in exactly one module
assert_contains "$ROOT/service/AsrEngine.cpp" 'qwen_asr.h'       "AsrEngine wraps qwen-asr"
assert_absent  "$ROOT/service/Server.cpp"     'qwen_asr.h'       "Server does not touch qwen-asr"
# Protocol verbs preserved in the C++ daemon
for v in FOCUS BLUR CTRL_DOWN TYPED; do
  assert_contains "$ROOT/service/VoiceSession.cpp" "$v" "verb $v handled"
done
# Hard-coded repo model path must never appear in setup scripts
assert_absent "$ROOT/scripts/setup-qwen-asr-0.6b.sh" "/home/hualet/projects/hualet/echoflow/model" "no hard-coded repo model path"
# Settings schema must match the new config (no stale keys; prompt + safetensors model_dir present)
assert_absent "$ROOT/ui-host/settings-schema.json" "asr_runner"     "settings schema drops asr_runner"
assert_absent "$ROOT/ui-host/settings-schema.json" "asr_project_dir" "settings schema drops asr_project_dir"
assert_absent "$ROOT/ui-host/settings-schema.json" "Qwen3-ASR-GGUF"  "settings schema has no GGUF default"
assert_contains "$ROOT/ui-host/settings-schema.json" '"prompt"'       "settings schema has prompt option"
assert_contains "$ROOT/ui-host/settings-schema.json" "qwen3-asr-0.6b" "settings schema points at safetensors dir"
assert_absent  "$ROOT/ui-host/EchoFlowSettings.cpp"  "asr_runner"      "EchoFlowSettings writes no asr_runner default"
# No PySide/PyQt anywhere
if grep -rEq "PySide|PyQt" "$ROOT"/service "$ROOT"/fcitx-addon "$ROOT"/ui-host 2>/dev/null; then
  echo "FAIL - no PySide/PyQt in C++ sources"; fail=$((fail+1))
else echo "ok   - no PySide/PyQt in C++ sources"; pass=$((pass+1)); fi

echo "---"
echo "spec: $pass passed, $fail failed"
[[ $fail -eq 0 ]]
```

- [ ] **Step 3: Run the whole suite**

```bash
ctest --test-dir build --output-on-failure
```
Expected: all QTest binaries + `spec_tests` pass.

- [ ] **Commit boundary:** `Add QTest harness + bash spec tests`

---

## Task 15: Removals + docs

**Files:**
- Delete: `echoflow/` (all), `pyproject.toml`, `uv.lock`, `echoflow.egg-info/`, `tests/*.py`
- Rewrite: `AGENTS.md`, `README.md`

- [ ] **Step 1: Delete the Python artifacts**

```bash
git rm -rf echoflow pyproject.toml uv.lock tests/*.py
rm -rf echoflow.egg-info
```

- [ ] **Step 2: Rewrite `AGENTS.md`** key sections to reflect the new world. At minimum:
  - **Stack:** drop Python/uv/llama.cpp/onnxruntime; add "C++17 daemon (`echoflow-service`), qwen-asr (pure C, submodule), OpenBLAS".
  - **Commands:** replace the `uv run python -m unittest` block with `cmake -S . -B build && cmake --build build && ctest --test-dir build`; drop `uv run echoflow-service --print-default-config` → `./build/service/echoflow-service --print-default-config`.
  - **Architecture boundaries:** "`service/AsrEngine.cpp` is the **only** module that includes `qwen_asr.h`" (replaces the `asr_runner.py` rule).
  - **Testing quirks:** "spec-as-test now lives in `tests/spec/run_spec.sh` (bash/grep); logic tests are QTest in `tests/`".
  - **Build the components:** single top-level `cmake` command; drop the per-component + llama-runtime blocks.
  - **Logging:** "C++ daemon: use the `echoflow::log()` helper from `service/log.h` (prefixes `[timestamp]`). Do not scatter `std::cout`."

- [ ] **Step 3: Rewrite `README.md`** for end users: new prerequisites (`libopenblas-dev`, Qt6/Fcitx5 dev), single `cmake` build, `git submodule update --init --recursive`, `scripts/setup-qwen-asr-0.6b.sh`, `./install-user.sh`.

- [ ] **Step 4: Final clean-tree verification**

```bash
git submodule update --init --recursive third_party/qwen-asr
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
bash -n install-user.sh uninstall-user.sh run.sh scripts/*.sh tests/spec/*.sh
```
Expected: full build + all tests green + shell syntax clean.

- [ ] **Commit boundary:** `Remove Python + llama.cpp; rewrite AGENTS.md/README for C++/qwen-asr`

---

## Self-review notes (plan author)

- **Spec coverage:** Every spec section maps to a task: §2 topology (Tasks 10–11), §3 daemon (4–10), §4 qwen-asr (1–2), §5 top-level CMake (3, 11), §6 model/config (4, 12), §7 install/systemd (13), §8 testing (4–7, 14), §9 removals (15).
- **Type consistency:** `Config::defaultConfig()` (Task 4) is used by Tasks 5, 9, 10; `IRecorder::stop()` returns `std::filesystem::path` (empty == cancelled) consistently in Tasks 5/8; `Committer(cfg, socketPath)` ctor signature matches Task 10's wiring.
- **Known delta from Python:** greedy decode (no `temperature=0.4`) — documented in Task 9 Step 2. Single-threaded server (matches Python) — Task 10 Step 2.

## Execution handoff

Plan complete and saved to `docs/superpowers/plans/2026-06-18-qwen-asr-cpp-port.md`. Two execution options:

1. **Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration.
2. **Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints.

Which approach?
