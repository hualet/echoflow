# Microphone Source Setting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a user-selectable microphone source for both live and file-based PipeWire recording.

**Architecture:** Store the selected PipeWire/PulseAudio source name in `basic.recording.source`; an empty value keeps system default behavior. Both recorder paths build their `pw-record` argv through small helpers so source handling is shared and testable. The settings UI populates the source combobox from `pactl list sources short`.

**Tech Stack:** C++17, Qt6/DTK settings, PipeWire `pw-record`, QTest.

---

### Task 1: Config and Recorder Command

**Files:**
- Modify: `service/Config.h`
- Modify: `service/Config.cpp`
- Modify: `service/Recorder.h`
- Modify: `service/Recorder.cpp`
- Modify: `service/PipeWireLiveVoicePipeline.cpp`
- Modify: `tests/test_config.cpp`
- Modify: `tests/test_recorder_command.cpp`
- Modify: `tests/CMakeLists.txt`

- [x] Add failing config and command tests.
- [x] Add `PipeWireRecordConfig::source`.
- [x] Parse `basic.recording.source`.
- [x] Add recorder argv helpers that include `--target <source>` only when source is non-empty.
- [x] Use the helpers from live and non-live recorder paths.
- [x] Run `test_config` and `test_recorder_command`.

### Task 2: Settings UI

**Files:**
- Modify: `ui-host/settings-schema.json`
- Modify: `ui-host/EchoFlowSettings.cpp`

- [x] Add `source` combobox to the recording settings group.
- [x] Include `basic.recording.source` when writing a new default config.
- [x] Populate the combobox with an empty "系统默认" entry plus current `pactl list sources short` source names.

### Task 3: Verification and Install

**Files:**
- Modify: source and test files from Tasks 1-2.

- [x] Run `git diff --check`.
- [x] Run `ctest --test-dir build --output-on-failure`.
- [x] Run `./install-user.sh`.
- [x] Restart `echoflow.service` and `echoflow-ui.service`.
- [x] Run `~/.local/bin/echoflow-service --self-test`.
- [x] Commit the plan and implementation together.
