# Merged Voice Capsule Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the cursor-following idle trigger button and the bottom-center recording capsule with a single fixed-position capsule that morphs idle/recording/transcribing, fades out in two stages when idle, and hides immediately when the user types.

**Architecture:** The fcitx addon gains a `TYPED` control command (observer-only, no key consumption). The Python service learns `TYPED` + a `typed_hidden` suppression flag so a typing-induced hide is not undone by the cursor-rect `FOCUS` chatter. The C++ UI host positions every show at a fixed bottom-center (`fixedCapsulePosition()`). The QML becomes one capsule with an explicit mode and a two-stage idle fade. Transports (sockets, ASR, commit) are unchanged.

**Tech Stack:** Python 3.11 (unittest, `uv run`), C++17 / Qt6 / Fcitx5 (CMake), QML (QtQuick 2.15).

**Commit policy (from AGENTS.md):** Do **not** run `git commit` unless the user explicitly asks. Each task below ends with a *Commit boundary* showing the suggested message to use only when the user requests a commit.

**Test runner (from AGENTS.md) — unittest, NOT pytest:**
- Full suite: `uv run python -m unittest discover -s tests -v`
- Single module: `uv run python -m unittest tests.test_service -v`
- Single case: `uv run python -m unittest tests.test_service.VoiceSessionTests.test_typed_hides_tooltip_when_idle`

Spec: `docs/superpowers/specs/2026-06-18-merged-voice-capsule-design.md`

---

## File structure

| File | Responsibility | Change |
|------|----------------|--------|
| `echoflow/service.py` | Toggle state machine; owns tooltip visibility + new typing suppression | Modify `VoiceSession.__init__`, `handle_command`, `ALLOWED_COMMANDS` |
| `fcitx-addon/echoflow.cpp` | Input-context events; right-Ctrl capture | Modify `handleKeyEvent` to emit `TYPED` |
| `ui-host/main.cpp` | UI socket → QML bridge | Add `fixedCapsulePosition()`; route `SHOW_TOOLTIP` to fixed pos |
| `qml/EchoFlowTooltip.qml` | The capsule UI | Full rewrite: one capsule, mode + two-stage fade |
| `tests/test_service.py` | Service unit tests | Add `TYPED`/suppression cases + protocol routing |
| `tests/test_ui_host.py` | Spec-as-test for C++ files | Add `TYPED`/`isModifier`/`fixedCapsulePosition` assertions |

---

## Task 1: Service — `TYPED` command + typing suppression

**Files:**
- Modify: `echoflow/service.py` (`VoiceSession.__init__` ~L439-451, `handle_command` ~L453-476, `ALLOWED_COMMANDS` ~L503)
- Test: `tests/test_service.py` (`VoiceSessionTests` ~L326, `ServerProtocolTests` ~L481)

This task is pure Python and fully unit-testable, so it goes TDD: tests first.

- [ ] **Step 1: Write the failing service tests**

Add these methods to the `VoiceSessionTests` class in `tests/test_service.py` (e.g. immediately after `test_focus_with_cursor_rect_forwards_tooltip_position`):

```python
    def test_typed_hides_tooltip_when_idle(self):
        ui = FakeUiNotifier()
        session = service.VoiceSession(
            service.Config.default(),
            recorder=FakeRecorder(),
            asr=FakeAsr(),
            committer=FakeCommitter(),
            ui=ui,
        )
        session.handle_command("FOCUS")
        ui.messages.clear()

        self.assertEqual(session.handle_command("TYPED"), "TYPING hide")
        self.assertEqual(ui.messages, ["HIDE_TOOLTIP"])
        self.assertTrue(session.typed_hidden)

    def test_focus_after_typed_stays_hidden(self):
        ui = FakeUiNotifier()
        session = service.VoiceSession(
            service.Config.default(),
            recorder=FakeRecorder(),
            asr=FakeAsr(),
            committer=FakeCommitter(),
            ui=ui,
        )
        session.handle_command("FOCUS")
        session.handle_command("TYPED")
        ui.messages.clear()

        self.assertEqual(
            session.handle_command("FOCUS 120 240 2 18"), "TOOLTIP suppressed"
        )
        self.assertEqual(ui.messages, [])

    def test_typed_while_recording_is_ignored(self):
        ui = FakeUiNotifier()
        session = service.VoiceSession(
            service.Config.default(),
            recorder=FakeRecorder(),
            asr=FakeAsr(),
            committer=FakeCommitter(),
            ui=ui,
        )
        session.handle_command("FOCUS")
        session.handle_command("CTRL_DOWN")
        ui.messages.clear()

        self.assertEqual(session.handle_command("TYPED"), "IGNORED")
        self.assertEqual(ui.messages, [])
        self.assertEqual(session.state, service.SessionState.RECORDING)

    def test_blur_clears_suppression_and_refocus_shows(self):
        ui = FakeUiNotifier()
        session = service.VoiceSession(
            service.Config.default(),
            recorder=FakeRecorder(),
            asr=FakeAsr(),
            committer=FakeCommitter(),
            ui=ui,
        )
        session.handle_command("FOCUS")
        session.handle_command("TYPED")
        session.handle_command("BLUR")
        ui.messages.clear()

        self.assertEqual(session.handle_command("FOCUS"), "TOOLTIP show")
        self.assertEqual(ui.messages, ["SHOW_TOOLTIP 按右 Ctrl 语音输入"])
        self.assertFalse(session.typed_hidden)
```

Add this method to the `ServerProtocolTests` class (e.g. after `test_protocol_routes_focus_with_cursor_rect`):

```python
    def test_protocol_routes_typed_command(self):
        session = mock.Mock()
        session.handle_command.return_value = "OK"

        self.assertEqual(service.handle_protocol_message(session, b"TYPED\n"), b"OK\n")
        session.handle_command.assert_called_once_with("TYPED")
```

- [ ] **Step 2: Run the new tests to verify they fail**

Run: `uv run python -m unittest tests.test_service.VoiceSessionTests.test_typed_hides_tooltip_when_idle tests.test_service.ServerProtocolTests.test_protocol_routes_typed_command -v`

Expected: FAIL — `handle_command("TYPED")` returns `"ERR unknown-command"` (TYPED not handled yet) and `typed_hidden` attribute does not exist.

- [ ] **Step 3: Add the `typed_hidden` field**

In `echoflow/service.py`, in `VoiceSession.__init__`, after the line `self.tooltip_visible = False`:

```python
        self.tooltip_visible = False
        self.typed_hidden = False
```

- [ ] **Step 4: Handle `TYPED`, suppress `FOCUS`, reset on `BLUR`**

Replace the `FOCUS` branch, the `BLUR` branch, and the trailing `return` in `handle_command`. The new `handle_command` body (from `if verb == "FOCUS":` through the end) is:

```python
        if verb == "FOCUS":
            self.tooltip_visible = True
            if self.typed_hidden:
                return "TOOLTIP suppressed"
            argument = argument.strip()
            suffix = f" {argument}" if argument else ""
            self.ui.send(f"SHOW_TOOLTIP{suffix} 按右 Ctrl 语音输入")
            return "TOOLTIP show"
        if verb == "BLUR":
            self.tooltip_visible = False
            self.typed_hidden = False
            if self.state == SessionState.RECORDING:
                self.recorder.stop()
            self.state = SessionState.IDLE
            self.ui.send("HIDE_TOOLTIP")
            return "TOOLTIP hide"
        if verb == "TYPED":
            if (
                self.state == SessionState.IDLE
                and self.tooltip_visible
                and not self.typed_hidden
            ):
                self.typed_hidden = True
                self.ui.send("HIDE_TOOLTIP")
                return "TYPING hide"
            return "IGNORED"
        if verb == "CTRL_DOWN":
            if self.state == SessionState.IDLE:
                return self._start_recording()
            if self.state == SessionState.RECORDING:
                return self._stop_transcribe_commit()
            return "TRANSCRIBING"
        return "ERR unknown-command"
```

- [ ] **Step 5: Allow `TYPED` through the protocol gate**

In `echoflow/service.py`, change:

```python
ALLOWED_COMMANDS = {"FOCUS", "BLUR", "CTRL_DOWN"}
```

to:

```python
ALLOWED_COMMANDS = {"FOCUS", "BLUR", "CTRL_DOWN", "TYPED"}
```

- [ ] **Step 6: Run the full service test module to verify it passes**

Run: `uv run python -m unittest tests.test_service -v`

Expected: PASS — all existing cases still green (message formats unchanged) plus the 5 new cases pass.

- [ ] **Step 7: Byte-compile sanity check**

Run: `uv run python -m py_compile echoflow/service.py && echo OK`

Expected: prints `OK`.

- [ ] **Step 8: Commit boundary (only when the user asks)**

```bash
git add echoflow/service.py tests/test_service.py
git commit -m "Hide voice capsule when the user types

Add a TYPED control command and a typed_hidden suppression flag in
VoiceSession so a typing-induced hide is not undone by the cursor-rect
FOCUS chatter. Suppression clears on BLUR; the next focus-in re-shows
the capsule. TYPED is ignored while recording or transcribing."
```

---

## Task 2: Fcitx addon — emit `TYPED` on non-modifier key presses

**Files:**
- Modify: `fcitx-addon/echoflow.cpp` (`handleKeyEvent` ~L245-259)
- Test: `tests/test_ui_host.py` (spec-as-test, `UiHostStructureTests`)

The C++ side has no runtime test harness; behavior is locked by spec-as-test
assertions that read the source as text. We add the assertion first (TDD-style),
then edit the source, then compile.

- [ ] **Step 1: Write the failing spec-as-test assertion**

In `tests/test_ui_host.py`, add this method to the `UiHostStructureTests` class (e.g. after `test_fcitx_addon_dedupes_right_ctrl_auto_repeat`):

```python
    def test_fcitx_addon_declutters_capsule_on_typing(self):
        addon_cpp = (ROOT / "fcitx-addon" / "echoflow.cpp").read_text(encoding="utf-8")

        self.assertIn("TYPED", addon_cpp)
        self.assertIn("isModifier()", addon_cpp)
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `uv run python -m unittest tests.test_ui_host.UiHostStructureTests.test_fcitx_addon_declutters_capsule_on_typing -v`

Expected: FAIL — `"TYPED"` not yet present in `echoflow.cpp`.

- [ ] **Step 3: Emit `TYPED` from `handleKeyEvent`**

In `fcitx-addon/echoflow.cpp`, replace the whole `handleKeyEvent` method:

```cpp
    void handleKeyEvent(fcitx::Event &event) {
        auto *keyEvent = static_cast<fcitx::KeyEvent *>(&event);
        const fcitx::Key key = keyEvent->key();

        if (isRightCtrl(key)) {
            if (keyEvent->isRelease()) {
                rightCtrlDown_ = false;
                return;
            }
            if (rightCtrlDown_) {
                return;
            }
            rightCtrlDown_ = true;
            sendControlCommand("CTRL_DOWN");
            return;
        }

        // Any other non-modifier key press means the user is typing/editing in
        // the focused input context, so tell the service to declutter the voice
        // capsule. Observer only: we never consume the event (no accept/filter).
        if (!keyEvent->isRelease() && !key.isModifier()) {
            sendControlCommand("TYPED");
        }
    }
```

- [ ] **Step 4: Run the spec-as-test to verify it passes**

Run: `uv run python -m unittest tests.test_ui_host -v`

Expected: PASS — all assertions green (existing right-Ctrl strings still present; new `TYPED` + `isModifier()` present).

- [ ] **Step 5: Build the addon to confirm it compiles**

Run: `cmake -S fcitx-addon -B build/fcitx-addon -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build/fcitx-addon`

Expected: builds with no errors.

- [ ] **Step 6: Commit boundary (only when the user asks)**

```bash
git add fcitx-addon/echoflow.cpp tests/test_ui_host.py
git commit -m "Notify service when the user types in the focused context

handleKeyEvent now sends a TYPED control command on any non-modifier
key press (right-Ctrl still toggles recording; other modifiers are
ignored). The event is observed but never consumed."
```

---

## Task 3: UI host — fixed capsule position

**Files:**
- Modify: `ui-host/main.cpp` (`applyMessage` ~L190-248)
- Test: `tests/test_ui_host.py` (spec-as-test)

The capsule is now always at one spot, so `SHOW_TOOLTIP` stops positioning at
the cursor and uses the same fixed bottom-center the recording capsule uses.

- [ ] **Step 1: Write the failing spec-as-test assertion**

In `tests/test_ui_host.py`, add this method to the `UiHostStructureTests` class (e.g. after `test_cpp_ui_host_builds_against_qt_qml`):

```python
    def test_ui_host_pins_capsule_to_fixed_bottom_center(self):
        main_cpp = (ROOT / "ui-host" / "main.cpp").read_text(encoding="utf-8")

        self.assertIn("fixedCapsulePosition", main_cpp)
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `uv run python -m unittest tests.test_ui_host.UiHostStructureTests.test_ui_host_pins_capsule_to_fixed_bottom_center -v`

Expected: FAIL — `fixedCapsulePosition` not yet present.

- [ ] **Step 3: Add the `fixedCapsulePosition` helper**

In `ui-host/main.cpp`, add this free function in the anonymous namespace, just before the `TooltipController` class (after `sendControlCommand`):

```cpp
struct TooltipPos {
    bool hasPosition = false;
    int x = 0;
    int y = 0;
};

// The voice capsule always sits at the primary screen's bottom-center, ~8px
// above the panel (availableGeometry already excludes panels).
TooltipPos fixedCapsulePosition() {
    if (auto *screen = QGuiApplication::primaryScreen()) {
        const QRect avail = screen->availableGeometry();
        return {true, avail.left() + avail.width() / 2, avail.bottom() - 8};
    }
    return {};
}
```

- [ ] **Step 4: Route `SHOW_TOOLTIP` and `RECORDING` through the fixed position**

In `ui-host/main.cpp`, replace the entire body of `applyMessage` (the private method in `UiSocketServer`) with:

```cpp
    void applyMessage(const QString &message) {
        if (message.startsWith(QStringLiteral("SHOW_TOOLTIP"))) {
            // The capsule is fixed in place; we only parse off any leading
            // "x y w h" cursor rect the service forwarded to recover the text.
            QString text = message.mid(QStringLiteral("SHOW_TOOLTIP").size()).trimmed();
            const QStringList parts = text.split(QChar(' '), Qt::SkipEmptyParts);
            if (parts.size() >= 4) {
                bool okX = false, okY = false, okW = false, okH = false;
                parts.at(0).toInt(&okX);
                parts.at(1).toInt(&okY);
                parts.at(2).toInt(&okW);
                parts.at(3).toInt(&okH);
                if (okX && okY && okW && okH) {
                    text = parts.mid(4).join(QChar(' '));
                }
            }
            if (text.isEmpty()) {
                text = QStringLiteral("按右 Ctrl 语音输入");
            }
            const TooltipPos pos = fixedCapsulePosition();
            controller_->setTooltip(true, text, false, pos.hasPosition, pos.x, pos.y);
        } else if (message == QStringLiteral("RECORDING")) {
            const TooltipPos pos = fixedCapsulePosition();
            controller_->setTooltip(true, QStringLiteral("正在聆听"), true,
                                     pos.hasPosition, pos.x, pos.y);
        } else if (message == QStringLiteral("TRANSCRIBING")) {
            controller_->setTooltip(true, QStringLiteral("正在转写"), true);
        } else if (message == QStringLiteral("HIDE_TOOLTIP") || message == QStringLiteral("IDLE")) {
            controller_->setTooltip(false, QString(), false);
        }
    }
```

Note: the slot `setTooltip(visible, message, busy, hasPosition, moveX, moveY)` and the signal still carry the `hasPosition` / `moveX` / `moveY` names, so the existing spec-as-test assertions (`SHOW_TOOLTIP`, `hasPosition`, `moveY`, `TRANSCRIBING`, `QSocketNotifier`) keep passing.

- [ ] **Step 5: Run the spec-as-test to verify it passes**

Run: `uv run python -m unittest tests.test_ui_host -v`

Expected: PASS.

- [ ] **Step 6: Build the UI host to confirm it compiles**

Run: `cmake -S ui-host -B build/ui-host -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build/ui-host`

Expected: builds with no errors.

- [ ] **Step 7: Commit boundary (only when the user asks)**

```bash
git add ui-host/main.cpp tests/test_ui_host.py
git commit -m "Pin the voice capsule to a fixed bottom-center position

SHOW_TOOLTIP no longer places the capsule at the cursor; both the idle
and recording states use a shared fixedCapsulePosition() at the primary
screen's bottom-center, matching the merged-capsule design."
```

---

## Task 4: QML — merged capsule with two-stage idle fade

**Files:**
- Modify: `qml/EchoFlowTooltip.qml` (full rewrite)

No automated test covers QML. Verification is: optional `qmllint` if installed,
plus a runtime smoke check, plus the full Python suite (still green since QML
is not imported by Python tests).

- [ ] **Step 1: Replace the QML file**

Overwrite `qml/EchoFlowTooltip.qml` entirely with:

```qml
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15

// Single fixed-position voice capsule. Morphs between idle (hint + mic button),
// recording (waveform + pause button), and transcribing (status text). When idle
// it fades out in two stages if the mouse is absent; the C++ host hides it
// outright when the service reports the user is typing.
Window {
    id: root
    flags: Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    visible: false

    property string message: ""
    property bool busy: false
    property int targetX: 0
    property int targetY: 0

    readonly property bool recording: root.busy && root.message === "正在聆听"
    readonly property bool transcribing: root.busy && root.message === "正在转写"
    readonly property bool idle: root.visible && !root.busy

    // (targetX, targetY) is the capsule's bottom-center anchor.
    width: capsule.width + 16
    height: capsule.height + 16
    x: targetX - width / 2
    y: targetY - height

    function stopFade() {
        fadeStage1.stop()
        fadeStage2.stop()
        hideAfterFade.stop()
    }

    Connections {
        target: tooltipController
        function onTooltipChanged(visibility, msg, isBusy, hasPosition, moveX, moveY) {
            root.message = msg
            root.busy = isBusy
            if (hasPosition) {
                root.targetX = moveX
                root.targetY = moveY
            }
            if (visibility) {
                root.visible = true
                root.stopFade()
                capsule.opacity = 1
                if (!isBusy) {
                    fadeStage1.restart()
                }
            } else {
                root.stopFade()
                root.visible = false
            }
        }
    }

    // stage 1: 2s of no mouse interaction -> opacity 0.1
    Timer {
        id: fadeStage1
        interval: 2000
        onTriggered: { capsule.opacity = 0.1; fadeStage2.restart() }
    }

    // stage 2: another 2s -> opacity 0, then hide
    Timer {
        id: fadeStage2
        interval: 2000
        onTriggered: { capsule.opacity = 0; hideAfterFade.start() }
    }

    // wait for the opacity animation (Behavior below) to finish before hiding
    Timer {
        id: hideAfterFade
        interval: 250
        onTriggered: {
            if (root.idle && capsule.opacity === 0) {
                root.visible = false
            }
        }
    }

    // ---------------- capsule body ----------------
    Rectangle {
        id: capsule
        anchors.centerIn: parent

        readonly property int kHeight: 40
        readonly property int kButtonSize: 32
        readonly property int kRadius: kHeight / 2
        readonly property int kHPad: 14
        readonly property int kGap: 12
        readonly property int kWaveBars: 11
        readonly property int kWaveBarWidth: 4
        readonly property int kWaveSpacing: 4
        readonly property int kWaveWidth: kWaveBars * kWaveBarWidth + (kWaveBars - 1) * kWaveSpacing

        height: kHeight
        radius: kRadius
        color: "#0f3d3e"
        border.color: "#18a6a7"
        border.width: 1
        clip: true

        width: root.recording
               ? waveArea.width + kHPad + kGap + kButtonSize + kHPad
               : (root.transcribing
                  ? statusLabel.implicitWidth + 2 * kHPad
                  : idleHint.implicitWidth + kGap + kButtonSize + 2 * kHPad)

        Behavior on width { NumberAnimation { duration: 250; easing.type: Easing.InOutQuad } }
        Behavior on opacity { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }

        // ---- recording: waveform (left) ----
        Item {
            id: waveArea
            anchors {
                left: parent.left
                leftMargin: capsule.kHPad
                verticalCenter: parent.verticalCenter
            }
            width: capsule.kWaveWidth
            height: parent.height
            visible: root.recording

            Row {
                anchors.centerIn: parent
                spacing: capsule.kWaveSpacing

                Repeater {
                    model: capsule.kWaveBars
                    Rectangle {
                        width: capsule.kWaveBarWidth
                        radius: width / 2
                        color: "#18a6a7"
                        height: [4, 10, 16, 22, 18, 14, 18, 22, 16, 10, 4][index]
                        transformOrigin: Item.Center
                        SequentialAnimation on scale {
                            running: root.visible && root.recording
                            loops: Animation.Infinite
                            PauseAnimation { duration: index * 60 }
                            NumberAnimation { to: 1.5; duration: 250; easing.type: Easing.InOutQuad }
                            NumberAnimation { to: 0.5; duration: 250; easing.type: Easing.InOutQuad }
                        }
                    }
                }
            }
        }

        // ---- idle: hint text (left) ----
        Label {
            id: idleHint
            anchors {
                left: parent.left
                leftMargin: capsule.kHPad
                verticalCenter: parent.verticalCenter
            }
            visible: root.idle
            text: "按右 Ctrl 语音输入"
            color: "#f4f6f8"
            font.pixelSize: 13
        }

        // ---- transcribing: centered status ----
        Label {
            id: statusLabel
            anchors.centerIn: parent
            visible: root.transcribing
            text: root.message
            color: "#f4f6f8"
            font.pixelSize: 13
        }

        // ---- right-side action button: mic (idle) / pause (recording) ----
        Rectangle {
            id: actionButton
            width: capsule.kButtonSize
            height: capsule.kButtonSize
            radius: width / 2
            color: "#18a6a7"
            visible: root.idle || root.recording
            anchors {
                right: parent.right
                rightMargin: capsule.kHPad
                verticalCenter: parent.verticalCenter
            }

            // mic glyph (idle)
            Column {
                anchors.centerIn: parent
                spacing: 1
                visible: root.idle
                Rectangle { width: 8; height: 12; radius: 4; color: "white"; anchors.horizontalCenter: parent.horizontalCenter }
                Rectangle { width: 2; height: 3; color: "white"; anchors.horizontalCenter: parent.horizontalCenter }
                Rectangle { width: 12; height: 2; radius: 1; color: "white"; anchors.horizontalCenter: parent.horizontalCenter }
            }

            // pause glyph (recording)
            Row {
                anchors.centerIn: parent
                spacing: 4
                visible: root.recording
                Repeater {
                    model: 2
                    Rectangle {
                        width: 4; height: 12; radius: 1; color: "white"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }

        // whole capsule is the hover + click target
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: (root.idle || root.recording) ? Qt.PointingHandCursor : Qt.ArrowCursor
            onEntered: {
                if (root.idle) {
                    root.stopFade()
                    capsule.opacity = 1
                }
            }
            onExited: {
                if (root.idle) {
                    root.stopFade()
                    fadeStage1.restart()
                }
            }
            onClicked: {
                if (root.idle || root.recording) {
                    tooltipController.requestToggle()
                }
            }
        }
    }
}
```

- [ ] **Step 2: Optional QML lint**

Run: `qmllint-qt6 qml/EchoFlowTooltip.qml 2>/dev/null || qmllint qml/EchoFlowTooltip.qml 2>/dev/null || echo "qmllint not installed; skipping"`

Expected: no output (or the "not installed; skipping" message). If it prints warnings, fix them.

- [ ] **Step 3: Confirm Python tests still pass (QML is not imported by them)**

Run: `uv run python -m unittest discover -s tests -v`

Expected: all green.

- [ ] **Step 4: Runtime smoke check (manual)**

With the service + ui-host running and an input focused, verify by eye:
- Idle capsule appears bottom-center with hint + mic button, full opacity.
- After ~2s of no mouse movement it dims to ~0.1; after ~2s more it vanishes.
- Moving the mouse over it restores full opacity and restarts the dim countdown.
- Clicking the mic button starts recording (waveform + pause appear at the same spot).
- Clicking the pause button stops recording and shows "正在转写", then commits and hides.
- Typing in the input hides the capsule immediately; it stays hidden until the input loses and regains focus.

- [ ] **Step 5: Commit boundary (only when the user asks)**

```bash
git add qml/EchoFlowTooltip.qml
git commit -m "Merge the trigger button and recording capsule into one fixed capsule

One window now morphs idle (hint + mic) / recording (waveform + pause) /
transcribing (status), always at the fixed bottom-center. Idle fades to
0.1 after 2s without mouse interaction and to 0 (hidden) after another
2s; hover restores it; busy states hold it fully visible."
```

---

## Task 5: Final verification

- [ ] **Step 1: Full Python test suite**

Run: `uv run python -m unittest discover -s tests -v`

Expected: all tests pass, including the new `TYPED`/suppression cases and the new spec-as-test assertions.

- [ ] **Step 2: Byte-compile all Python sources**

Run: `uv run python -m py_compile echoflow/*.py tests/*.py && echo OK`

Expected: prints `OK`.

- [ ] **Step 3: CLI sanity checks (from AGENTS.md)**

Run:
```bash
uv run echoflow-service --print-default-config
uv run qwen-asr-transcribe --help
python3 -m json.tool config.example.json > /dev/null
```

Expected: each command succeeds (config printed, help printed, JSON valid).

- [ ] **Step 4: Build both C++ components**

Run:
```bash
cmake -S ui-host -B build/ui-host -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build/ui-host
cmake -S fcitx-addon -B build/fcitx-addon -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build/fcitx-addon
```

Expected: both build with no errors.

- [ ] **Step 5: Shell script syntax checks (from AGENTS.md)**

Run: `bash -n install-user.sh uninstall-user.sh run.sh scripts/*.sh`

Expected: no output (all scripts parse).

---

## Self-review notes

- **Spec coverage:** §2.1 states → Task 4 QML. §2.2 opacity lifecycle → Task 4 (fadeStage1/fadeStage2/hideAfterFade + hover). §2.3 hide-on-typing → Task 1 (service TYPED/suppression) + Task 2 (addon emits TYPED) + Task 4 (HIDE_TOOLTIP → hide). §2.4 fixed-once-hidden → Task 1 (suppression) + Task 4 (fade ends in `visible=false`). §3.2 fixed position → Task 3. All sections covered.
- **Type/name consistency:** `TYPED` command string matches across addon (`sendControlCommand("TYPED")`), service (`verb == "TYPED"`, `ALLOWED_COMMANDS`), and test. `typed_hidden` matches across service + tests. `fixedCapsulePosition` matches across main.cpp + test. QML `stopFade()` referenced consistently.
- **Unchanged message formats:** `SHOW_TOOLTIP x y w h 按右 Ctrl 语音输入`, `RECORDING`, `TRANSCRIBING`, `HIDE_TOOLTIP`, `IDLE` — preserved, so the existing `test_focus_emits_*` and protocol tests stay green.
