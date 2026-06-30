# Capsule Window Bounds Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the blurred top-level tooltip window match the voice capsule's exact bounds so no larger rounded background is rendered.

**Architecture:** Keep the existing DTK blur and capsule component unchanged. Protect the intended window-to-capsule size relationship with source-level spec assertions, then replace the two expanded window-size bindings with direct capsule-size bindings.

**Tech Stack:** Qt 6 QML, DTK declarative controls, POSIX shell spec tests, CMake/CTest

---

### Task 1: Match the tooltip window to the capsule

**Files:**
- Modify: `tests/spec/run_spec.sh`
- Modify: `qml/EchoFlowTooltip.qml:46-47`

- [ ] **Step 1: Write the failing regression assertions**

Add these assertions beside the existing tooltip material checks in
`tests/spec/run_spec.sh`:

```bash
assert_contains "$ROOT/qml/EchoFlowTooltip.qml" "width: capsule.width" "tooltip window width matches capsule"
assert_contains "$ROOT/qml/EchoFlowTooltip.qml" "height: capsule.height" "tooltip window height matches capsule"
assert_absent "$ROOT/qml/EchoFlowTooltip.qml" "capsule.width + 16" "tooltip has no horizontal blur margin"
assert_absent "$ROOT/qml/EchoFlowTooltip.qml" "capsule.height + 16" "tooltip has no vertical blur margin"
```

- [ ] **Step 2: Run the spec suite and verify the new assertions fail**

Run:

```bash
bash tests/spec/run_spec.sh
```

Expected: two new `assert_contains` checks fail and two new `assert_absent`
checks fail because the QML still adds 16 pixels to both dimensions.

- [ ] **Step 3: Implement the minimal QML correction**

Replace the expanded bindings in `qml/EchoFlowTooltip.qml` with:

```qml
width: capsule.width
height: capsule.height
```

Do not change the capsule's own dimensions, radius, material, position formulas,
animation, or interaction.

- [ ] **Step 4: Run focused verification**

Run:

```bash
bash tests/spec/run_spec.sh
```

Expected: all spec assertions pass with zero failures.

- [ ] **Step 5: Run build and full regression verification**

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: the build exits successfully and CTest reports zero failed tests. If
Unix socket tests fail with `Operation not permitted` inside the sandbox, rerun
CTest with the required sandbox escalation before classifying the result.

- [ ] **Step 6: Commit the focused implementation**

```bash
git add qml/EchoFlowTooltip.qml tests/spec/run_spec.sh docs/superpowers/plans/2026-06-30-capsule-window-bounds.md
git commit -m "fix(ui): match tooltip window to capsule" -m "Remove the extra blurred window margin so the capsule follows the same content-sized window pattern as dde-osd. Add focused spec assertions for the window bounds."
```
