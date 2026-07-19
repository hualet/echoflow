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

assert_before() {
  local file="$1"
  local first="$2"
  local second="$3"
  local description="$4"
  local first_line second_line
  first_line="$(grep -nF -m1 -- "$first" "$file" | cut -d: -f1)"
  second_line="$(grep -nF -m1 -- "$second" "$file" | cut -d: -f1)"
  if [[ -n "$first_line" && -n "$second_line" && "$first_line" -lt "$second_line" ]]; then
    echo "ok   - $description"
    pass=$((pass + 1))
  else
    echo "FAIL - $description ($first must appear before $second in $file)"
    fail=$((fail + 1))
  fi
}

assert_png_asset() {
  local file="$1"
  local description="$2"
  local dimensions=""
  if [[ -s "$file" ]]; then
    dimensions="$(python3 - "$file" <<'PY'
import struct
import sys

with open(sys.argv[1], "rb") as png:
    signature = png.read(24)
if signature[:8] != b"\x89PNG\r\n\x1a\n" or signature[12:16] != b"IHDR":
    raise SystemExit("not a PNG with an IHDR header")
print(*struct.unpack(">II", signature[16:24]))
PY
)"
  fi
  if [[ "$dimensions" == "768 768" ]]; then
    echo "ok   - $description"
    pass=$((pass + 1))
  else
    echo "FAIL - $description ($file is missing, empty, or ${dimensions:-unreadable})"
    fail=$((fail + 1))
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
assert_contains "$ROOT/install-user.sh" 'systemctl --user enable echoflow.service echoflow-ui.service' "install-user.sh always enables user services"
assert_contains "$ROOT/install-user.sh" 'systemctl --user restart echoflow.service echoflow-ui.service' "started installs restart both user services"
assert_absent "$ROOT/install-user.sh" 'systemctl --user enable --now' "install-user.sh does not leave running services on old binaries"
assert_before "$ROOT/install-user.sh" 'if [[ "$START_SERVICES" == "1" ]]' 'systemctl --user restart echoflow.service echoflow-ui.service' "service restart is guarded by START_SERVICES"
assert_before "$ROOT/install-user.sh" 'systemctl --user enable echoflow.service echoflow-ui.service' 'if [[ "$START_SERVICES" == "1" ]]' "no-start still enables services before skipping restart"

assert_absent "$ROOT/CMakeLists.txt" "qwen-asr-runtime" "top-level no longer builds qwen-asr-runtime"
assert_contains "$ROOT/CMakeLists.txt" 'set(ECHOFLOW_CPU_TARGET "native" CACHE STRING' "CMake exposes the CPU target"
assert_contains "$ROOT/CMakeLists.txt" 'set(GGML_NATIVE OFF CACHE BOOL "" FORCE)' "portable target disables GGML native tuning"
assert_contains "$ROOT/CMakeLists.txt" 'set(GGML_AVX2 ON CACHE BOOL "" FORCE)' "portable target enables AVX2"
assert_contains "$ROOT/CMakeLists.txt" 'set(GGML_FMA ON CACHE BOOL "" FORCE)' "portable target enables FMA"
assert_contains "$ROOT/CMakeLists.txt" 'set(GGML_F16C ON CACHE BOOL "" FORCE)' "portable target enables F16C"
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
assert_contains "$ROOT/ui-host/settings-schema.json" '"key": "getting_started"' "settings schema has Getting started group"
assert_contains "$ROOT/ui-host/settings-schema.json" '"name": "开始使用"' "settings schema names Getting started group"
assert_contains "$ROOT/ui-host/settings-schema.json" '"key": "usage_guide"' "settings schema has Usage guide option"
assert_contains "$ROOT/ui-host/settings-schema.json" '"name": "使用引导"' "settings schema names Usage guide option"
assert_contains "$ROOT/ui-host/settings-schema.json" '"type": "guide"' "settings schema uses guide widget type"
assert_before "$ROOT/ui-host/settings-schema.json" '"key": "getting_started"' '"key": "basic"' "Getting started precedes Basic settings"
assert_contains "$ROOT/ui-host/settings-schema.json" "download_0.6b" "settings schema has 0.6B download row key"
assert_contains "$ROOT/ui-host/settings-schema.json" "download_1.7b" "settings schema has 1.7B download row key"
assert_contains "$ROOT/ui-host/settings-schema.json" "Qwen3-ASR-0.6B" "settings schema lists 0.6B model row"
assert_contains "$ROOT/ui-host/settings-schema.json" "Qwen3-ASR-1.7B" "settings schema lists 1.7B model row"
assert_contains "$ROOT/ui-host/settings-schema.json" "hf-mirror" "settings schema has hf-mirror download source"
assert_absent "$ROOT/ui-host/settings-schema.json" "model_dir" "settings schema has no model_dir option"
assert_absent "$ROOT/install-user.sh" "model_dir" "install-user.sh writes no model_dir"
assert_contains "$ROOT/ui-host/SettingsDialog.cpp" "modeldownload" "SettingsDialog registers modeldownload factory"
assert_contains "$ROOT/ui-host/SettingsDialog.cpp" 'registerWidget(QStringLiteral("guide")' "SettingsDialog registers guide factory"
assert_contains "$ROOT/ui-host/SettingsDialog.cpp" 'setObjectName(QStringLiteral("usageGuideButton"))' "Usage guide button has stable object name"
assert_contains "$ROOT/ui-host/SettingsDialog.cpp" 'emit usageGuideRequested()' "Usage guide button emits request signal"
assert_contains "$ROOT/ui-host/SettingsDialog.h" 'void usageGuideRequested();' "SettingsDialog exposes Usage guide request signal"
assert_before "$ROOT/ui-host/SettingsDialog.cpp" 'registerWidget(QStringLiteral("guide")' 'updateSettings(settings)' "guide factory is registered before settings rows are built"
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
assert_contains "$ROOT/service/main.cpp" '\"vad_backend\"' "default config reports selected VAD backend"
assert_contains "$ROOT/service/main.cpp" '\"energy_min_speech_rms\"' "default config reports energy VAD floor"
assert_contains "$ROOT/service/CrispLiveVoicePipeline.h" "readerError_" "live pipeline retains capture thread failures"
assert_contains "$ROOT/service/CrispLiveVoicePipeline.cpp" "rethrow_exception(readerError_)" "live pipeline rejects partial text after capture failure"
# The capsule appears only when recording starts and has no idle hint or
# focus-tracking fade timers anymore.
assert_contains "$ROOT/qml/EchoFlowTooltip.qml" "tooltipController.requestCancel()" "capsule X button cancels recording"
assert_contains "$ROOT/qml/EchoFlowTooltip.qml" "tooltipController.requestToggle()" "capsule check button stops and transcribes"
assert_absent "$ROOT/qml/EchoFlowTooltip.qml" "按右 Ctrl 语音输入" "capsule has no idle focus hint"
assert_absent "$ROOT/qml/EchoFlowTooltip.qml" "fadeStage1" "capsule has no staged fade timer"
assert_absent "$ROOT/qml/EchoFlowTooltip.qml" "appearFade" "capsule has no appear-fade timer"
assert_contains "$ROOT/qml/EchoFlowTooltip.qml" "import org.deepin.dtk 1.0 as D" "tooltip imports DTK QML controls"
assert_contains "$ROOT/qml/EchoFlowTooltip.qml" "D.DWindow.enableBlurWindow: root.visible && capsule.opacity > 0" "tooltip blur follows capsule visibility"
assert_contains "$ROOT/qml/EchoFlowTooltip.qml" "D.DWindow.enabled: true" "tooltip enables DTK window handling"
assert_contains "$ROOT/qml/EchoFlowTooltip.qml" "D.DWindow.windowRadius: capsule.kRadius" "tooltip window radius matches capsule"
assert_contains "$ROOT/qml/EchoFlowTooltip.qml" "D.StyledBehindWindowBlur" "capsule uses DTK background blur"
assert_contains "$ROOT/qml/EchoFlowTooltip.qml" "D.OutsideBoxBorder" "capsule uses notification-style outside border"
assert_contains "$ROOT/qml/EchoFlowTooltip.qml" "D.InsideBoxBorder" "capsule uses notification-style inside border"
assert_absent "$ROOT/qml/EchoFlowTooltip.qml" "org.deepin.ds.notification" "capsule avoids shell-private notification dependency"
assert_contains "$ROOT/qml/EchoFlowTooltip.qml" "width: capsule.width" "tooltip window width matches capsule"
assert_contains "$ROOT/qml/EchoFlowTooltip.qml" "height: capsule.height" "tooltip window height matches capsule"
assert_absent "$ROOT/qml/EchoFlowTooltip.qml" "capsule.width + 16" "tooltip has no horizontal blur margin"
assert_absent "$ROOT/qml/EchoFlowTooltip.qml" "capsule.height + 16" "tooltip has no vertical blur margin"

assert_contains "$ROOT/ui-host/CMakeLists.txt" "ModelDownloadCoordinator.cpp" "ui-host builds ModelDownloadCoordinator"
for source in UiActivationServer.cpp UiActivationHost.cpp OnboardingState.cpp SetupCommandRunner.cpp ModelSetupAdapter.cpp OnboardingSetupController.cpp OnboardingDialog.cpp; do
  assert_contains "$ROOT/ui-host/CMakeLists.txt" "$source" "ui-host builds $source"
done
assert_contains "$ROOT/ui-host/CMakeLists.txt" '${CMAKE_CURRENT_LIST_DIR}/../service' "standalone ui-host resolves service includes from its own directory"
assert_contains "$ROOT/ui-host/CMakeLists.txt" "icons.qrc" "ui-host embeds app icon resources"
assert_contains "$ROOT/ui-host/CMakeLists.txt" "onboarding.qrc" "ui-host embeds onboarding illustration resources"
if grep -qF '<!DOCTYPE RCC>' "$ROOT/ui-host/onboarding.qrc" \
    && grep -qF '<RCC version="1.0">' "$ROOT/ui-host/onboarding.qrc" \
    && grep -qF '<qresource prefix="/onboarding">' "$ROOT/ui-host/onboarding.qrc"; then
  echo "ok   - onboarding resources use the canonical RCC format and prefix"
  pass=$((pass + 1))
else
  echo "FAIL - onboarding resources use the canonical RCC format and prefix"
  fail=$((fail + 1))
fi
for illustration in intro shortcut settings setup; do
  assert_png_asset "$ROOT/ui-host/onboarding/${illustration}.png" "onboarding ${illustration} illustration exists on the shared 768px canvas"
  assert_contains "$ROOT/ui-host/onboarding.qrc" "<file alias=\"${illustration}.png\">onboarding/${illustration}.png</file>" "onboarding qrc aliases ${illustration}.png"
done
onboarding_test_resource_count="$(grep -cF '../ui-host/onboarding.qrc' "$ROOT/tests/CMakeLists.txt")"
onboarding_test_autorcc_count="$(grep -cF 'set_property(TARGET test_onboarding_dialog PROPERTY AUTORCC ON)' "$ROOT/tests/CMakeLists.txt")"
if [[ "$onboarding_test_resource_count" -eq 2 && "$onboarding_test_autorcc_count" -eq 2 ]]; then
  echo "ok   - both focused onboarding dialog targets compile onboarding resources"
  pass=$((pass + 1))
else
  echo "FAIL - both focused onboarding dialog targets compile onboarding resources (found $onboarding_test_resource_count references and $onboarding_test_autorcc_count AUTORCC settings)"
  fail=$((fail + 1))
fi
assert_contains "$ROOT/ui-host/CMakeLists.txt" 'configure_file(' "ui-host configures desktop entry"
assert_contains "$ROOT/ui-host/CMakeLists.txt" '"${CMAKE_CURRENT_SOURCE_DIR}/echoflow.desktop.in"' "desktop entry source works in standalone builds"
assert_contains "$ROOT/ui-host/CMakeLists.txt" '"${CMAKE_CURRENT_BINARY_DIR}/echoflow.desktop"' "desktop entry is generated in the current build tree"
assert_contains "$ROOT/ui-host/CMakeLists.txt" 'DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"' "ui-host installs desktop entry to applications"
assert_contains "$ROOT/ui-host/CMakeLists.txt" '"${CMAKE_CURRENT_SOURCE_DIR}/icons/echoflow.svg"' "ui-host installs EchoFlow icon"
assert_contains "$ROOT/ui-host/CMakeLists.txt" 'DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"' "ui-host installs scalable app icon"
assert_contains "$ROOT/ui-host/echoflow.desktop.in" '[Desktop Entry]' "desktop entry has standard header"
assert_contains "$ROOT/ui-host/echoflow.desktop.in" 'Type=Application' "desktop entry declares application type"
assert_contains "$ROOT/ui-host/echoflow.desktop.in" 'Name=EchoFlow' "desktop entry has English name"
assert_contains "$ROOT/ui-host/echoflow.desktop.in" 'Name[zh_CN]=EchoFlow 语音输入' "desktop entry has Chinese name"
assert_contains "$ROOT/ui-host/echoflow.desktop.in" 'Comment=Offline voice input for Fcitx5' "desktop entry has English comment"
assert_contains "$ROOT/ui-host/echoflow.desktop.in" 'Comment[zh_CN]=离线、安全、流畅的语音输入' "desktop entry has Chinese comment"
assert_contains "$ROOT/ui-host/echoflow.desktop.in" '# SPDX-FileCopyrightText: 2026 Hualet Wang' "desktop entry has copyright header"
assert_contains "$ROOT/ui-host/echoflow.desktop.in" '# SPDX-License-Identifier: GPL-3.0-or-later' "desktop entry has license header"
assert_contains "$ROOT/ui-host/echoflow.desktop.in" 'Exec="@CMAKE_INSTALL_FULL_BINDIR@/echoflow-ui" --activate' "desktop entry activates the configured EchoFlow UI"
assert_contains "$ROOT/ui-host/echoflow.desktop.in" 'Icon=echoflow' "desktop entry uses installed EchoFlow icon"
assert_contains "$ROOT/ui-host/echoflow.desktop.in" 'Terminal=false' "desktop entry does not open a terminal"
assert_contains "$ROOT/ui-host/echoflow.desktop.in" 'Categories=Utility;Accessibility;' "desktop entry has utility and accessibility categories"
assert_contains "$ROOT/ui-host/echoflow.desktop.in" 'StartupNotify=true' "desktop entry enables startup notification"
assert_absent "$ROOT/ui-host/echoflow.desktop.in" "$ROOT" "desktop entry has no source-tree absolute path"
assert_contains "$ROOT/ui-host/main.cpp" ":/icons/echoflow.svg" "tray icon uses EchoFlow logo resource"
assert_absent "$ROOT/ui-host/main.cpp" "SP_ComputerIcon" "tray icon no longer uses generic computer icon"
assert_contains "$ROOT/ui-host/main.cpp" 'QStringLiteral("activate")' "ui-host accepts explicit activation option"
assert_contains "$ROOT/ui-host/main.cpp" 'UiActivationHost activationHost(defaultUiLockPath())' "ui-host uses threaded activation host"
assert_contains "$ROOT/ui-host/main.cpp" 'UiActivationServer::Result::ActivatedExisting' "secondary activation exits through existing instance"
assert_contains "$ROOT/ui-host/main.cpp" 'UiActivationServer::Result::Failed' "instance acquisition failure is handled"
assert_absent "$ROOT/ui-host/main.cpp" 'acquireUiInstanceServer' "legacy inline instance acquisition is removed"
assert_contains "$ROOT/ui-host/main.cpp" 'bool pendingActivation' "early activation is queued until UI readiness"
assert_contains "$ROOT/ui-host/main.cpp" 'activationHost.acquire(pendingActivation' "startup activation is routed through the threaded host"
assert_contains "$ROOT/ui-host/main.cpp" 'OnboardingState onboardingState' "onboarding state has process lifetime"
assert_contains "$ROOT/ui-host/main.cpp" 'QProcessSetupCommandRunner setupCommandRunner' "setup command runner has process lifetime"
assert_contains "$ROOT/ui-host/main.cpp" 'ModelSetupAdapter modelSetupAdapter' "model adapter has process lifetime"
assert_contains "$ROOT/ui-host/main.cpp" 'OnboardingSetupController onboardingController' "setup controller has process lifetime"
assert_contains "$ROOT/ui-host/main.cpp" 'ModelDownloadCoordinator::instance()' "onboarding observes process-lifetime download coordinator"
assert_contains "$ROOT/ui-host/main.cpp" 'auto showOnboarding = ' "ui-host shares lazy onboarding entry point"
assert_contains "$ROOT/ui-host/main.cpp" 'usageGuideRequested' "settings Usage guide opens onboarding"
assert_contains "$ROOT/ui-host/main.cpp" 'finishedAndSettingsRequested' "onboarding completion opens settings"
assert_contains "$ROOT/ui-host/main.cpp" 'QObject::tr("使用引导")' "tray exposes Usage guide action"
assert_before "$ROOT/ui-host/main.cpp" 'QObject::tr("使用引导")' 'QObject::tr("设置")' "tray Usage guide precedes Settings"
assert_before "$ROOT/ui-host/main.cpp" 'QObject::tr("设置")' 'trayMenu.addSeparator()' "tray separator follows Settings"
assert_before "$ROOT/ui-host/main.cpp" 'trayMenu.addSeparator()' 'QObject::tr("退出")' "tray Quit follows separator"
assert_contains "$ROOT/ui-host/main.cpp" 'bool shuttingDown = false' "ui-host tracks application shutdown"
assert_contains "$ROOT/ui-host/main.cpp" '&QCoreApplication::aboutToQuit' "ui-host cleans dialogs before app.exec returns"
assert_contains "$ROOT/ui-host/main.cpp" 'shuttingDown = true' "shutdown guard is set before dialog cleanup"
assert_contains "$ROOT/ui-host/main.cpp" 'delete settingsDialog.data()' "settings dialog is deleted synchronously during shutdown"
assert_contains "$ROOT/ui-host/main.cpp" 'delete onboardingDialog.data()' "onboarding dialog is deleted synchronously during shutdown"
assert_contains "$ROOT/ui-host/main.cpp" 'if (!shuttingDown)' "service restart is suppressed during application shutdown"
assert_before "$ROOT/ui-host/main.cpp" 'shuttingDown = true' 'delete settingsDialog.data()' "shutdown guard precedes settings dialog deletion"
assert_before "$ROOT/ui-host/main.cpp" '&QCoreApplication::aboutToQuit' 'return app.exec()' "dialog cleanup is connected while captured stack state is alive"
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
assert_contains "$ROOT/debian/control" "desktop-file-utils" "debian build dependencies provide desktop-file-validate"
if [[ ! -e "$ROOT/debian/postinst" ]]; then
  echo "ok   - Debian uses debhelper-generated user-service maintainer scripts"
  pass=$((pass + 1))
else
  echo "FAIL - manual Debian postinst would duplicate debhelper user-service restart"
  fail=$((fail + 1))
fi
assert_contains "$ROOT/debian/control" "libdtk6widget-dev (>= 6.7)" "debian control keeps DTK widget dependency at minor version"
assert_absent "$ROOT/debian/control" "libdtk6widget-dev (>= 6.7." "debian control does not pin DTK widget dependency to patch/build"
assert_contains "$ROOT/debian/shlibs.local" "libdtk6widget 6 libdtk6widget (>= 6.7)" "debian shlibs keeps DTK widget runtime dependency at minor version"
assert_absent "$ROOT/debian/shlibs.local" ">= 6.7." "debian shlibs does not pin DTK runtime dependency to patch/build"
assert_contains "$ROOT/debian/control" "fcitx5" "debian package depends on fcitx5"
assert_contains "$ROOT/fcitx-addon/echoflow.conf.in" "Library=libechoflow" "Fcitx addon loads the packaged module filename"
assert_contains "$ROOT/install-user.sh" "command -v fcitx5" "user install detects Fcitx before reloading"
assert_contains "$ROOT/uninstall-user.sh" "command -v fcitx5" "user uninstall detects Fcitx before reloading"
assert_contains "$ROOT/uninstall-user.sh" '$PREFIX/share/applications/echoflow.desktop' "user uninstall removes desktop entry"
assert_contains "$ROOT/uninstall-user.sh" '$PREFIX/share/icons/hicolor/scalable/apps/echoflow.svg' "user uninstall removes app icon"
assert_contains "$ROOT/debian/control" "pipewire-bin" "debian package depends on PipeWire tools"
assert_contains "$ROOT/debian/rules" "dh $@" "debian rules delegates to debhelper"
assert_contains "$ROOT/CMakeLists.txt" '"${CMAKE_BINARY_DIR}/systemd/user/echoflow.service"' "CMake installs the service user unit for debhelper"
assert_contains "$ROOT/CMakeLists.txt" '"${CMAKE_BINARY_DIR}/systemd/user/echoflow-ui.service"' "CMake installs the UI user unit for debhelper"
dh_sequence="$(cd "$ROOT" && dh binary --no-act 2>&1)"
if grep -qF 'dh_installsystemduser' <<< "$dh_sequence"; then
  echo "ok   - standard debhelper sequence handles systemd user units"
  pass=$((pass + 1))
else
  echo "FAIL - standard debhelper sequence missing dh_installsystemduser"
  fail=$((fail + 1))
fi
assert_contains "$ROOT/debian/rules" 'override_dh_auto_configure:' "Debian overrides CMake configuration"
assert_contains "$ROOT/debian/rules" '-- -DECHOFLOW_CPU_TARGET=x86-64-v3' "Debian selects deterministic CPU target"
assert_contains "$ROOT/debian/source/format" "3.0 (native)" "debian source format is native"
assert_contains "$ROOT/.github/workflows/build.yml" "cmake --build build" "build workflow builds CMake tree"
assert_contains "$ROOT/.github/workflows/build.yml" "ctest --test-dir build --output-on-failure" "build workflow runs tests"
assert_contains "$ROOT/.github/workflows/build.yml" "desktop-file-utils" "build workflow installs desktop-file-validate"
assert_contains "$ROOT/.github/workflows/deb.yml" "dpkg-buildpackage -us -uc -b" "deb workflow builds binary package"
assert_contains "$ROOT/.github/workflows/deb.yml" "desktop-file-utils" "deb workflow installs desktop-file-validate"
assert_contains "$ROOT/.github/workflows/deb.yml" "softprops/action-gh-release" "deb workflow publishes release assets"
assert_contains "$ROOT/AGENTS.md" "scripts/check-release-performance.py" "version bump instructions require a benchmark"
assert_contains "$ROOT/AGENTS.md" "release-only commit" "version bump instructions require isolated release metadata"

if bash "$ROOT/tests/spec/test_onboarding_install.sh"; then
  pass=$((pass + 1))
else
  echo "FAIL - executable install and uninstall behavior"
  fail=$((fail + 1))
fi

if bash "$ROOT/tests/spec/test_ui_host_install.sh"; then
  pass=$((pass + 1))
else
  echo "FAIL - standalone UI configure, build, and DESTDIR install"
  fail=$((fail + 1))
fi

echo "---"
echo "spec: $pass passed, $fail failed"
[[ $fail -eq 0 ]]
