# Capsule Notification Material Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the existing voice capsule a deepin notification-style blurred background with matching inner and outer edge lines without changing its geometry or behavior.

**Architecture:** Keep the change inside the existing QML window. Enable DTK window blur, make the capsule's `Rectangle` transparent, and layer DTK's public blur and border primitives behind and above the existing content; guard the material structure with the repository's shell spec tests.

**Tech Stack:** Qt 6 QML, DTK Declarative 1.0, Bash spec tests, CMake/CTest

---

## File Structure

- Modify `tests/spec/run_spec.sh`: assert that the capsule uses DTK blur and
  both notification-style border primitives without importing the private
  dde-shell notification module.
- Modify `qml/EchoFlowTooltip.qml`: define theme-aware material colors and
  layer the DTK blur, outside border, and inside border inside the unchanged
  capsule geometry.

### Task 1: Guard the DTK notification material

**Files:**
- Modify: `tests/spec/run_spec.sh:96`
- Test: `tests/spec/run_spec.sh`

- [ ] **Step 1: Write the failing source-level assertions**

Insert these assertions after the existing tooltip opacity assertion:

```bash
assert_contains "$ROOT/qml/EchoFlowTooltip.qml" "import org.deepin.dtk 1.0 as D" "tooltip imports DTK QML controls"
assert_contains "$ROOT/qml/EchoFlowTooltip.qml" "D.DWindow.enableBlurWindow: true" "tooltip enables window blur"
assert_contains "$ROOT/qml/EchoFlowTooltip.qml" "D.StyledBehindWindowBlur" "capsule uses DTK background blur"
assert_contains "$ROOT/qml/EchoFlowTooltip.qml" "D.OutsideBoxBorder" "capsule uses notification-style outside border"
assert_contains "$ROOT/qml/EchoFlowTooltip.qml" "D.InsideBoxBorder" "capsule uses notification-style inside border"
assert_absent "$ROOT/qml/EchoFlowTooltip.qml" "org.deepin.ds.notification" "capsule avoids shell-private notification dependency"
```

- [ ] **Step 2: Run the spec test and verify RED**

Run: `bash tests/spec/run_spec.sh`

Expected: FAIL for the five missing DTK import/material assertions; the
private-module assertion passes.

### Task 2: Build the notification-style capsule material

**Files:**
- Modify: `qml/EchoFlowTooltip.qml:4-27,112-137`
- Test: `tests/spec/run_spec.sh`

- [ ] **Step 1: Import DTK and enable blur on the tooltip window**

Add the public DTK QML import after the Qt imports:

```qml
import org.deepin.dtk 1.0 as D
```

Enable blur next to the transparent `Window` color:

```qml
color: "transparent"
D.DWindow.enableBlurWindow: true
```

- [ ] **Step 2: Define notification-matched material colors**

Add these read-only properties after `capsuleBackground`:

```qml
readonly property color capsuleBlend: Qt.rgba(capsuleBackground.r,
                                                capsuleBackground.g,
                                                capsuleBackground.b,
                                                D.DTK.themeType === D.ApplicationHelper.DarkType ? 0.6 : 0.4)
readonly property color capsuleOutsideBorder: D.DTK.themeType === D.ApplicationHelper.DarkType
                                                ? Qt.rgba(0, 0, 0, 0.6)
                                                : Qt.rgba(0, 0, 0, 0.1)
readonly property color capsuleInsideBorder: D.DTK.themeType === D.ApplicationHelper.DarkType
                                               ? Qt.rgba(1, 1, 1, 0.1)
                                               : Qt.rgba(1, 1, 1, 0.2)
```

Keep `capsuleBackground` as the ThemeBridge-derived base color. The alpha
values match the notification/OSD crystal treatment while preserving the
application's palette hue.

- [ ] **Step 3: Replace the opaque fill and single border with DTK layers**

Change the capsule surface settings to:

```qml
color: "transparent"
clip: false
```

Remove the existing `border.color` and `border.width`. Insert these children
before the recording waveform so they share the existing animated capsule
width and radius:

```qml
D.StyledBehindWindowBlur {
    anchors.fill: parent
    control: root
    cornerRadius: capsule.kRadius
    blendColor: root.capsuleBlend
}

D.OutsideBoxBorder {
    anchors.fill: parent
    radius: capsule.kRadius
    color: root.capsuleOutsideBorder
    z: D.DTK.AboveOrder
}

D.InsideBoxBorder {
    anchors.fill: parent
    radius: capsule.kRadius
    color: root.capsuleInsideBorder
    z: D.DTK.AboveOrder
}
```

The tooltip window already has eight pixels of transparent margin around the
capsule, so the one-pixel outside border remains inside the window. Leaving
the border layers mouse-transparent preserves the existing full-capsule
`MouseArea` behavior.

- [ ] **Step 4: Run the focused spec test and verify GREEN**

Run: `bash tests/spec/run_spec.sh`

Expected: `spec: <count> passed, 0 failed`, including all six new assertions.

- [ ] **Step 5: Check QML syntax and DTK type resolution**

Run: `/usr/lib/qt6/bin/qmllint qml/EchoFlowTooltip.qml`

Expected: exit code 0. DTK types may produce unresolved-type warnings on
systems whose package omits `dtk6declarative.qmltypes`; syntax errors are not
acceptable.

### Task 3: Verify and commit the focused change

**Files:**
- Modify: `docs/superpowers/plans/2026-06-30-capsule-notification-material.md`
- Modify: `qml/EchoFlowTooltip.qml`
- Modify: `tests/spec/run_spec.sh`

- [ ] **Step 1: Build the project**

Run: `cmake --build build`

Expected: all targets, including `echoflow-ui`, build successfully.

- [ ] **Step 2: Run the complete automated test suite**

Run: `ctest --test-dir build --output-on-failure`

Expected: all tests pass. If Unix-socket tests fail with `EPERM` under the
sandbox, rerun outside the sandbox before classifying the result as a code
failure.

- [ ] **Step 3: Inspect the final diff**

Run: `git diff --check && git diff -- qml/EchoFlowTooltip.qml tests/spec/run_spec.sh docs/superpowers/plans/2026-06-30-capsule-notification-material.md`

Expected: no whitespace errors; only the planned material, assertions, and
plan document are present.

- [ ] **Step 4: Commit the implementation**

```bash
git add qml/EchoFlowTooltip.qml tests/spec/run_spec.sh docs/superpowers/plans/2026-06-30-capsule-notification-material.md
git commit -m "feat(ui): add notification-style capsule material" \
  -m "Use DTK blur and dual edge lines so the voice capsule matches deepin notifications while retaining its existing shape and behavior."
```
