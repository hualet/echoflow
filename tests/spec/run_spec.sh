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

assert_contains "$ROOT/CMakeLists.txt" "qwen-asr-runtime" "top-level builds qwen-asr-runtime"
assert_contains "$ROOT/CMakeLists.txt" "add_subdirectory(fcitx-addon)" "top-level builds fcitx-addon"
assert_contains "$ROOT/CMakeLists.txt" "add_subdirectory(ui-host)" "top-level builds ui-host"
assert_contains "$ROOT/qwen-asr-runtime/CMakeLists.txt" "USE_OPENBLAS" "qwen-asr runtime enables OpenBLAS"

assert_contains "$ROOT/service/AsrEngine.h" 'qwen_asr.h' "AsrEngine is qwen-asr boundary"
assert_absent "$ROOT/service/Server.cpp" 'qwen_asr.h' "Server does not touch qwen-asr"
assert_absent "$ROOT/service/VoiceSession.cpp" 'qwen_asr.h' "VoiceSession does not touch qwen-asr"
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
assert_contains "$ROOT/service/ModelCatalog.h" "Qwen/Qwen3-ASR-1.7B" "ModelCatalog knows the 1.7B repo"

assert_absent "$ROOT/ui-host/settings-schema.json" "asr_runner" "settings schema drops asr_runner"
assert_absent "$ROOT/ui-host/settings-schema.json" "asr_project_dir" "settings schema drops asr_project_dir"
assert_absent "$ROOT/ui-host/settings-schema.json" "Qwen3-ASR-GGUF" "settings schema has no GGUF default"
assert_contains "$ROOT/ui-host/settings-schema.json" '"prompt"' "settings schema has prompt option"
assert_contains "$ROOT/ui-host/settings-schema.json" "qwen3-asr-0.6b" "settings schema points at safetensors dir"
assert_absent "$ROOT/ui-host/EchoFlowSettings.cpp" "asr_runner" "EchoFlowSettings writes no asr_runner default"
assert_contains "$ROOT/ui-host/EchoFlowSettings.cpp" "basic.recognition.prompt" "EchoFlowSettings writes prompt default"

assert_contains "$ROOT/ui-host/CMakeLists.txt" "ModelDownloadCoordinator.cpp" "ui-host builds ModelDownloadCoordinator"
assert_contains "$ROOT/ui-host/ModelRowWidget.cpp" "ModelDownloadCoordinator" "ModelRowWidget talks to the coordinator"
assert_absent  "$ROOT/ui-host/ModelRowWidget.cpp" "new ModelDownloader" "ModelRowWidget no longer owns a downloader"

if grep -rEq "PySide|PyQt" "$ROOT"/service "$ROOT"/fcitx-addon "$ROOT"/ui-host 2>/dev/null; then
  echo "FAIL - no PySide/PyQt in native sources"
  fail=$((fail + 1))
else
  echo "ok   - no PySide/PyQt in native sources"
  pass=$((pass + 1))
fi

echo "---"
echo "spec: $pass passed, $fail failed"
[[ $fail -eq 0 ]]
