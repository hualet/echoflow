# Merged Voice Capsule — Design Spec

- Date: 2026-06-18
- Status: Approved (design review)
- Components: `qml/EchoFlowTooltip.qml`, `ui-host/main.cpp`, `echoflow/service.py`, `fcitx-addon/echoflow.cpp`, `tests/`

## 1. Goal

Replace the two separate UI controls that exist today — a cursor-following
**idle trigger button** and a bottom-center **recording capsule** that never
appear at once — with a **single capsule fixed at bottom-center** that morphs
between idle / recording / transcribing states. Add an opacity lifecycle and a
hide-on-typing behavior so the capsule declutters itself when the user is
typing or idle.

Voice input itself is unchanged: right-Ctrl toggles recording; the ASR
subprocess, commit path, and sockets are untouched.

## 2. User-facing behavior

The capsule always sits at the primary screen's bottom-center, ~8px above the
panel (same spot the recording capsule uses today).

### 2.1 States

| State | Trigger | Visuals | Click action |
|-------|---------|---------|--------------|
| **Idle** | Input focused, not recording | Pill: hint text `按右 Ctrl 语音输入` (left) + mic button (right) | Click mic button → start recording |
| **Recording** | `RECORDING` message | Pill: waveform animation (left) + pause button (right) | Click pause → stop → transcribe → commit |
| **Transcribing** | `TRANSCRIBING` message | Pill: centered status text `正在转写` | none |

The right-side action button morphs mic → pause; the left side morphs
hint text → waveform. Capsule width animates.

### 2.2 Opacity lifecycle (idle state only)

1. On focus (idle capsule shown): opacity = 1, start `fadeStage1` (2 s).
2. `fadeStage1` fires: animate opacity → **0.1** over ~200 ms, start
   `fadeStage2` (2 s).
3. `fadeStage2` fires: animate opacity → **0**, then set `visible = false`.
4. Mouse enter/move on the capsule: opacity = 1, cancel both timers. Mouse
   exit: restart `fadeStage1`.
5. **Busy override:** when `RECORDING` or `TRANSCRIBING` arrives, force
   opacity = 1, `visible = true`, cancel fade timers (the capsule must stay
   fully visible while busy).

### 2.3 Hide on typing (new)

- The fcitx addon observes key events in the focused input context. On any
  **non-modifier key press** (letters, digits, punctuation, space, Backspace,
  arrows, etc. — i.e. the user is using the keyboard), it sends a new `TYPED`
  command to the service. Modifier keys (Shift, Alt, Ctrl-L, Super) and the
  right-Ctrl (which toggles recording) are **excluded**.
- The service, if currently **idle** and the tooltip is shown, enters a
  `typed_hidden_` (suppressed) state and emits `HIDE_TOOLTIP` → the capsule
  hides immediately (opacity 0, `visible = false`) regardless of fade stage.
- While suppressed, subsequent `FOCUS` / cursor-rect messages from the addon do
  **not** re-show the capsule. Suppression is cleared on `BLUR` (focus out);
  the next genuine focus-in re-shows it.
- `TYPED` is ignored while recording or transcribing (never hide mid-recording).
- **Hiding never disables voice input.** Right-Ctrl always toggles recording,
  and starting a recording via Ctrl re-shows the capsule (recording visuals).

### 2.4 "Fixed once hidden" rule

Whether hidden by the fade (§2.2 step 3) or by typing (§2.3), the capsule only
re-appears on the **next focus-in**, or when a recording starts via right-Ctrl.
This applies the "next focus only" decision consistently to both hide paths.

## 3. Component changes

### 3.1 `qml/EchoFlowTooltip.qml` (largest change)

- Remove the dual-mode geometry (trigger button at cursor vs. capsule at
  bottom-center). The window is always the capsule positioned at
  `(targetX, targetY)`, which the host now always sets to bottom-center.
- Introduce an explicit `mode`: `"idle" | "recording" | "transcribing"`,
  derived from `busy` / `message` (busy && "正在聆听" → recording;
  busy && "正在转写" → transcribing; else idle).
- Idle layout: hint `Label` (left) + mic `Rectangle` button (right). Mic
  `onClicked` → `tooltipController.requestToggle()`.
- Recording layout: existing `waveArea` (left) + `pauseButton` (right),
  unchanged visuals.
- Transcribing layout: centered status `Label`.
- Replace the old single `fadeTimer` (1.5 s → 0.2) with the two-stage timer
  sequence + opacity `Behavior` from §2.2. Gate the timers on `mode === "idle"`;
  reset on hover; cancel on busy.
- Drop the now-unused `isTrigger`/`triggerBtn`/`kFloat` geometry.

### 3.2 `ui-host/main.cpp`

- Add `fixedCapsulePosition()` helper returning bottom-center
  (`availableGeometry` center X, `bottom - 8`) — the same math the current
  `RECORDING` handler uses.
- `SHOW_TOOLTIP` handler: stop positioning at the cursor; instead always pass
  the fixed bottom-center position. Still parse the message to extract the hint
  text; the received cursor coordinates are ignored for positioning.
- `RECORDING` handler: reuse `fixedCapsulePosition()` (behavior unchanged).
- `TRANSCRIBING` / `HIDE_TOOLTIP` / `IDLE`: unchanged.
- **No new UI socket message** — typing-hide reuses `HIDE_TOOLTIP`.
- Must keep spec-as-test substrings present: `QSocketNotifier`,
  `SHOW_TOOLTIP`, `hasPosition`, `moveY`, `TRANSCRIBING`.

### 3.3 `echoflow/service.py`

- Add `typed_hidden_: bool` field to `VoiceSession` (default `False`).
- New `TYPED` verb in `handle_command`:
  - If `state == IDLE` and `tooltip_visible` and not `typed_hidden_`:
    set `typed_hidden_ = True`, `self.ui.send("HIDE_TOOLTIP")`,
    return `"TYPING hide"`.
  - Else (recording/transcribing, or already suppressed, or not visible):
    no-op; do not change state or send anything. Return `"IGNORED"`.
- `FOCUS`: keep the existing `SHOW_TOOLTIP <coords> 按右 Ctrl 语音输入`
  message format (unchanged) — **but** if `typed_hidden_` is `True`, skip
  sending `SHOW_TOOLTIP` (stay hidden) and return a suppressed ack
  (`"TOOLTIP suppressed"`). `tooltip_visible` stays `True` logically.
- `BLUR`: reset `typed_hidden_ = False` in addition to existing hide behavior.
- Add `"TYPED"` to `ALLOWED_COMMANDS`.
- Existing FOCUS/CTRL_DOWN/BLUR message formats are unchanged, so existing
  service tests remain green.

### 3.4 `fcitx-addon/echoflow.cpp`

- In `handleKeyEvent`, after the existing right-Ctrl toggle path (which
  `return`s), add: if the event is a press (not release) and the key is **not**
  a modifier, call `sendControlCommand("TYPED")`.
- Right-Ctrl and other modifiers are excluded (right-Ctrl is handled first and
  returns; others are filtered by the modifier check).
- **Observer only** — do not consume/filter/accept key events. Preserves
  existing behavior and the spec-as-test assertions (`isRightCtrl`,
  `FcitxKey_Control_R`, `CTRL_DOWN`, `rightCtrlDown_` present; `->accept()`,
  `isPlainCtrl`, `Control_L`, `CTRL_UP`, `filterAndAccept` absent).

### 3.5 Tests

- `tests/test_service.py`: add cases for:
  - `TYPED` while idle + shown → hides (`HIDE_TOOLTIP`), `typed_hidden_` set.
  - `TYPED` while recording → no-op (no hide, no state change).
  - Suppressed `FOCUS` does not re-show (no `SHOW_TOOLTIP`).
  - `BLUR` clears suppression; a subsequent `FOCUS` re-shows.
  - Add `TYPED` to any `ALLOWED_COMMANDS` coverage.
- `tests/test_ui_host.py`: spec-as-test — keep all current assertions green
  after the `main.cpp` edits. Optionally assert `fixedCapsulePosition` exists.
- QML is not spec-tested; no test constraint there.

## 4. Data flow summary (unchanged transports)

```
fcitx addon ──(control sock)──▶ service ──(ui sock)──▶ ui-host ──▶ QML
  FOCUS x y w h                   SHOW_TOOLTIP x y w h 按右 Ctrl 语音输入
  CTRL_DOWN                       RECORDING / TRANSCRIBING / IDLE
  BLUR                            HIDE_TOOLTIP
  TYPED  ◀── new                  HIDE_TOOLTIP  ◀── reused for typing-hide
```

## 5. Decisions logged (from design review)

1. After the 4 s fade the capsule is fully hidden (`visible = false`); it
   returns only on next focus-in or when a recording starts via Ctrl.
2. "Typing" = any non-modifier key press (arrows/Backspace included).
3. Idle hint text stays `按右 Ctrl 语音输入` (right-Ctrl remains the canonical
   trigger; minimizes test churn).

## 6. Out of scope

- No packaging, no CI, no new deps.
- No change to ASR, recording, commit, or socket transports.
- No settings UI.
