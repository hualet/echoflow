#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
pass=0
fail=0

assert_contains() {
  local file="$1"
  local needle="$2"
  local description="$3"
  if grep -qF -- "$needle" "$file"; then
    echo "ok   - $description"
    pass=$((pass + 1))
  else
    echo "FAIL - $description ($file missing: $needle)"
    fail=$((fail + 1))
  fi
}

assert_absent() {
  local file="$1"
  local needle="$2"
  local description="$3"
  if grep -qF -- "$needle" "$file"; then
    echo "FAIL - $description ($file should not contain: $needle)"
    fail=$((fail + 1))
  else
    echo "ok   - $description"
    pass=$((pass + 1))
  fi
}

assert_absent "$ROOT/install-user.sh" "uv venv" "install-user.sh has no uv venv"
assert_absent "$ROOT/install-user.sh" "uv pip" "install-user.sh has no uv pip"
assert_absent "$ROOT/install-user.sh" "llama" "install-user.sh has no llama"
assert_absent "$ROOT/install-user.sh" "asr_runner" "install-user.sh has no asr_runner key"
assert_absent "$ROOT/install-user.sh" "asr_project_dir" "install-user.sh has no asr_project_dir key"
assert_contains "$ROOT/install-user.sh" 'cmake -S "$ROOT_DIR"' "install-user.sh configures root CMake"
assert_contains "$ROOT/install-user.sh" 'cmake --install "$BUILD_DIR"' "install-user.sh installs root build"
assert_contains "$ROOT/install-user.sh" '$BUILD_DIR/systemd/user/echoflow.service' "install-user.sh installs configured service unit"
assert_contains "$ROOT/install-user.sh" '$BUILD_DIR/systemd/user/echoflow-ui.service' "install-user.sh installs configured UI service unit"

assert_absent "$ROOT/CMakeLists.txt" "qwen-asr-runtime" "top-level no longer builds qwen-asr-runtime"
assert_contains "$ROOT/CMakeLists.txt" 'set(CRISPASR_OPUS OFF CACHE BOOL "" FORCE)' "EchoFlow disables unused CrispASR Opus decoding"
assert_contains "$ROOT/CMakeLists.txt" 'list(FILTER _crispasr_sources INCLUDE REGEX "(^|/)(crispasr|crispasr_c_api)\\.cpp$")' "EchoFlow only compiles the required CrispASR session sources"
assert_contains "$ROOT/CMakeLists.txt" 'EXCLUDE REGEX "audioseal|snac|crispasr-llama-core"' "EchoFlow filters unused CrispASR link dependencies"
assert_contains "$ROOT/CMakeLists.txt" 'list(FILTER _crispasr_interface_links EXCLUDE REGEX' "EchoFlow filters transitive CrispASR link dependencies"
assert_absent "$ROOT/CMakeLists.txt" 'file(WRITE "${_crispasr_cmakelists}"' "CMake configure does not rewrite CrispASR sources"
assert_absent "$ROOT/service/CMakeLists.txt" "qwen_asr" "service no longer links qwen_asr"
assert_contains "$ROOT/CMakeLists.txt" "add_subdirectory(fcitx-addon)" "top-level builds fcitx-addon"
assert_contains "$ROOT/CMakeLists.txt" "add_subdirectory(ui-host)" "top-level builds ui-host"
assert_contains "$ROOT/CMakeLists.txt" 'DESTINATION "lib/systemd/user"' "top-level installs systemd user units in systemd search path"
assert_contains "$ROOT/systemd/user/echoflow.service.in" "@CMAKE_INSTALL_FULL_BINDIR@/echoflow-service" "service unit uses configured service path"
assert_contains "$ROOT/systemd/user/echoflow-ui.service.in" "@CMAKE_INSTALL_FULL_BINDIR@/echoflow-ui" "UI service unit uses configured UI path"

assert_contains "$ROOT/service/CrispAsrEngine.h" 'IAsrEngine' "CrispAsrEngine implements IAsrEngine"
assert_absent "$ROOT/service/Server.cpp" 'qwen_asr' "Server does not touch qwen-asr"
assert_absent "$ROOT/service/VoiceSession.cpp" 'qwen_asr' "VoiceSession does not touch qwen-asr"
for verb in FOCUS BLUR CTRL_DOWN TYPED; do
  assert_contains "$ROOT/service/VoiceSession.cpp" "$verb" "verb $verb handled"
done

assert_script_absent() {
  if [[ ! -e "$ROOT/scripts/setup-qwen-asr-0.6b.sh" ]]; then
    echo "ok   - setup script removed"
    pass=$((pass + 1))
  else
    echo "FAIL - setup script should be removed ($ROOT/scripts/setup-qwen-asr-0.6b.sh still exists)"
    fail=$((fail + 1))
  fi
}
assert_script_absent

assert_contains "$ROOT/ui-host/settings-schema.json" "modeldownload" "settings schema has modeldownload widget type"
assert_contains "$ROOT/ui-host/settings-schema.json" "download_0.6b" "settings schema has 0.6B download row key"
assert_contains "$ROOT/ui-host/settings-schema.json" "download_1.7b" "settings schema has 1.7B download row key"
assert_contains "$ROOT/ui-host/settings-schema.json" "Qwen3-ASR-0.6B" "settings schema lists 0.6B model row"
assert_contains "$ROOT/ui-host/settings-schema.json" "Qwen3-ASR-1.7B" "settings schema lists 1.7B model row"
assert_contains "$ROOT/ui-host/settings-schema.json" "hf-mirror" "settings schema has hf-mirror download source"
assert_absent "$ROOT/ui-host/settings-schema.json" "model_dir" "settings schema has no model_dir option"
assert_absent "$ROOT/install-user.sh" "model_dir" "install-user.sh writes no model_dir"
assert_contains "$ROOT/ui-host/SettingsDialog.cpp" "modeldownload" "SettingsDialog registers modeldownload factory"
assert_contains "$ROOT/service/ModelCatalog.h" "cstr/qwen3-asr-1.7b-GGUF" "ModelCatalog knows the 1.7B GGUF repo"

assert_absent "$ROOT/ui-host/settings-schema.json" "asr_runner" "settings schema drops asr_runner"
assert_absent "$ROOT/ui-host/settings-schema.json" "asr_project_dir" "settings schema drops asr_project_dir"
assert_absent "$ROOT/ui-host/settings-schema.json" "Qwen3-ASR-GGUF" "settings schema has no GGUF default"
assert_absent "$ROOT/ui-host/settings-schema.json" '"prompt"' "settings schema hides prompt option"
assert_absent "$ROOT/ui-host/settings-schema.json" "strip_trailing_punctuation" "settings schema hides strip punctuation option"
assert_contains "$ROOT/ui-host/settings-schema.json" "qwen3-asr-0.6b" "settings schema points at safetensors dir"
assert_contains "$ROOT/ui-host/settings-schema.json" "stream_transcription" "settings schema has stream transcription switch"
assert_contains "$ROOT/ui-host/settings-schema.json" "save_live_debug_audio" "settings schema has live debug audio switch"
assert_absent "$ROOT/ui-host/EchoFlowSettings.cpp" "asr_runner" "EchoFlowSettings writes no asr_runner default"
assert_absent "$ROOT/ui-host/EchoFlowSettings.cpp" "basic.recognition.prompt" "EchoFlowSettings writes no prompt default"
assert_absent "$ROOT/ui-host/EchoFlowSettings.cpp" "basic.recognition.strip_trailing_punctuation" "EchoFlowSettings writes no strip punctuation default"
assert_contains "$ROOT/ui-host/EchoFlowSettings.cpp" "basic.recognition.stream_transcription" "EchoFlowSettings writes stream transcription default"
assert_contains "$ROOT/service/main.cpp" "PipeWireRecorder" "service keeps non-stream recorder path"
assert_contains "$ROOT/qml/EchoFlowTooltip.qml" "opacityAnimationDuration = 500" "tooltip appears with 500ms opacity animation"

assert_contains "$ROOT/ui-host/CMakeLists.txt" "ModelDownloadCoordinator.cpp" "ui-host builds ModelDownloadCoordinator"
assert_contains "$ROOT/ui-host/CMakeLists.txt" "icons.qrc" "ui-host embeds app icon resources"
assert_contains "$ROOT/ui-host/main.cpp" ":/icons/echoflow.svg" "tray icon uses EchoFlow logo resource"
assert_absent "$ROOT/ui-host/main.cpp" "SP_ComputerIcon" "tray icon no longer uses generic computer icon"
assert_contains "$ROOT/ui-host/ModelRowWidget.cpp" "ModelDownloadCoordinator" "ModelRowWidget talks to the coordinator"
assert_contains "$ROOT/ui-host/ModelRowWidget.cpp" "onCoordinatorStateChanged" "ModelRowWidget handles coordinator state changes"
assert_contains "$ROOT/ui-host/ModelRowWidget.cpp" "snapshot(modelId())" "ModelRowWidget reads the coordinator snapshot"
assert_absent "$ROOT/ui-host/ModelRowWidget.cpp" "new ModelDownloader" "ModelRowWidget no longer owns a downloader"

if grep -rEq "PySide|PyQt" "$ROOT"/service "$ROOT"/fcitx-addon "$ROOT"/ui-host 2>/dev/null; then
  echo "FAIL - no PySide/PyQt in native sources"
  fail=$((fail + 1))
else
  echo "ok   - no PySide/PyQt in native sources"
  pass=$((pass + 1))
fi

assert_contains "$ROOT/debian/control" "Package: echoflow" "debian control defines echoflow package"
assert_contains "$ROOT/debian/control" "debhelper-compat (= 13)" "debian control uses debhelper compat 13"
assert_contains "$ROOT/debian/control" "libdtk6widget-dev (>= 6.7)" "debian control keeps DTK widget dependency at minor version"
assert_absent "$ROOT/debian/control" "libdtk6widget-dev (>= 6.7." "debian control does not pin DTK widget dependency to patch/build"
assert_contains "$ROOT/debian/shlibs.local" "libdtk6widget 6 libdtk6widget (>= 6.7)" "debian shlibs keeps DTK widget runtime dependency at minor version"
assert_absent "$ROOT/debian/shlibs.local" ">= 6.7." "debian shlibs does not pin DTK runtime dependency to patch/build"
assert_contains "$ROOT/debian/control" "fcitx5" "debian package depends on fcitx5"
assert_contains "$ROOT/debian/control" "pipewire-bin" "debian package depends on PipeWire tools"
assert_contains "$ROOT/debian/rules" "dh $@" "debian rules delegates to debhelper"
assert_contains "$ROOT/debian/source/format" "3.0 (native)" "debian source format is native"
assert_contains "$ROOT/.github/workflows/build.yml" "cmake --build build" "build workflow builds CMake tree"
assert_contains "$ROOT/.github/workflows/build.yml" "ctest --test-dir build --output-on-failure" "build workflow runs tests"
assert_contains "$ROOT/.github/workflows/deb.yml" "dpkg-buildpackage -us -uc -b" "deb workflow builds binary package"
assert_contains "$ROOT/.github/workflows/deb.yml" "softprops/action-gh-release" "deb workflow publishes release assets"

echo "---"
echo "spec: $pass passed, $fail failed"
[[ $fail -eq 0 ]]
