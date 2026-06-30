# Capsule Live Text Scroll Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cap the recording capsule at one third of the current screen width and smoothly reveal appended live text from right to left after it reaches that cap.

**Architecture:** Keep capsule sizing and presentation in `EchoFlowTooltip.qml`. Replace the width-limited, elided live label with a clipped viewport whose child label retains its natural width and animates to a right-aligned overflow offset; retain shell specification checks as the focused regression test.

**Tech Stack:** Qt Quick 2.15 QML, DTK QML, POSIX shell specification tests, CMake/CTest

---

### Task 1: Add failing capsule overflow specification checks

**Files:**
- Modify: `tests/spec/run_spec.sh:101-114`
- Test: `tests/spec/run_spec.sh`

- [ ] **Step 1: Write the failing test**

Add these assertions beside the existing tooltip checks:

```sh
assert_contains "$ROOT/qml/EchoFlowTooltip.qml" "readonly property real kMaxRecordingWidth: root.screen.width / 3" "recording capsule is capped at one third of its screen"
assert_contains "$ROOT/qml/EchoFlowTooltip.qml" "id: liveTextViewport" "live text uses a dedicated viewport"
assert_contains "$ROOT/qml/EchoFlowTooltip.qml" "clip: true" "live text viewport clips overflow"
assert_contains "$ROOT/qml/EchoFlowTooltip.qml" "x: Math.min(0, liveTextViewport.width - implicitWidth)" "overflowing live text keeps its newest suffix visible"
assert_contains "$ROOT/qml/EchoFlowTooltip.qml" "Behavior on x" "live text scrolls smoothly as partial results grow"
assert_absent "$ROOT/qml/EchoFlowTooltip.qml" "elide: Text.ElideLeft" "live text is scrolled instead of elided"
```

- [ ] **Step 2: Run the focused test to verify it fails**

Run: `tests/spec/run_spec.sh`

Expected: FAIL for the new maximum-width, viewport, clipping, overflow-offset,
and animation assertions because the QML still uses a 420-pixel elided label.

- [ ] **Step 3: Commit the red test with the implementation after Task 2**

Do not commit a deliberately failing intermediate revision. Keep the test
change unstaged until the implementation passes.

### Task 2: Cap the capsule and animate live-text overflow

**Files:**
- Modify: `qml/EchoFlowTooltip.qml:127-231`
- Test: `tests/spec/run_spec.sh`

- [ ] **Step 1: Define recording width bounds**

Add the screen-derived limit and natural recording width to `capsule`:

```qml
readonly property real kMaxRecordingWidth: root.screen.width / 3
readonly property real naturalRecordingWidth: waveArea.width + kHPad
                                               + (root.hasLiveText ? kGap + liveText.implicitWidth : 0)
                                               + kGap + kButtonSize + kButtonInset
```

Clamp only recording mode in the existing `width` binding:

```qml
width: root.recording
       ? Math.min(naturalRecordingWidth, kMaxRecordingWidth)
       : (root.transcribing
          ? statusLabel.implicitWidth + 2 * kHPad
          : idleHint.implicitWidth + kGap + kButtonSize + kHPad + kButtonInset)
```

- [ ] **Step 2: Replace the elided label with a clipped viewport**

Use the available space between the waveform and action button:

```qml
Item {
    id: liveTextViewport
    anchors {
        left: waveArea.right
        leftMargin: capsule.kGap
        right: actionButton.left
        rightMargin: capsule.kGap
        verticalCenter: parent.verticalCenter
    }
    height: liveText.implicitHeight
    visible: root.hasLiveText
    clip: true

    Label {
        id: liveText
        text: root.message
        color: root.capsuleText
        font.pixelSize: 13
        x: Math.min(0, liveTextViewport.width - implicitWidth)
        width: implicitWidth
        Behavior on x {
            NumberAnimation { duration: 180; easing.type: Easing.OutQuad }
        }
    }
}
```

Remove the old label's `width`, right alignment, and left-elide behavior.

- [ ] **Step 3: Run the focused test to verify it passes**

Run: `tests/spec/run_spec.sh`

Expected: all specification checks pass with a final `PASS` summary.

- [ ] **Step 4: Build and run the complete test suite**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: configuration and build succeed; CTest reports 100% passing tests.

- [ ] **Step 5: Check the patch and commit**

Run:

```bash
git diff --check
git status --short
git add qml/EchoFlowTooltip.qml tests/spec/run_spec.sh docs/superpowers/plans/2026-06-30-capsule-live-text-scroll.md
git commit -m "feat(ui): scroll overflowing live text" -m "Cap the recording capsule at one third of its screen.\nKeep new partial recognition text visible with a clipped animated viewport."
```

Expected: only the intended QML, test, and plan files are committed.
