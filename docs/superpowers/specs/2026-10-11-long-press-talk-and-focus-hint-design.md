# Long-Press Talk and Input Focus Hint Design

Date: 2026-10-11

## Goal

Make voice input discoverable and usable with the mouse alone, in the style
of WeChat's input box:

- holding the left mouse button inside a text field records speech, and
  releasing the button transcribes and commits it;
- when a text field gains focus, a small hint near the caret tells the user
  that voice input is available;
- right `Ctrl` follows the same hold-to-talk model: pressing it starts
  recording and releasing it transcribes and commits;
- the capsule stays at a fixed spot in the lower middle of the screen rather
  than following the input field.

## User Experience

### Focus Hint

When an input context gains focus and reports a caret rectangle, a small
DTK-styled pill (`EchoFlowHint.qml`, blurred rounded rect with the DTK
double border, matching the capsule) appears just below the caret line
(above it when there is no room below). It reads `长按鼠标或右 Ctrl 说话`,
or `按住右 Ctrl 说话` when long-press is disabled or unavailable.

The hint shows once per program run: the fcitx addon reports the first
focus of a program as `FOCUS` and every later focus as `REFOCUS`, which
refreshes caret geometry without requesting another hint. A program whose
input contexts have all been destroyed (it quit or restarted) gets one
more hint on its next focus. Focus reports within 500 ms of a program's
first focus still count as that first focus, so Chromium's per-click
blur/refocus churn cannot swallow the one hint.

The hint disappears after about 3 seconds, as soon as the user types, when
focus leaves the field, or when a voice session starts. It never takes focus
or mouse input, so clicks pass through to the application underneath.

The hint is not shown for password fields, while the persistent capsule is
pinned from the tray, or while a voice session is active.

### Long-Press Talk

Pressing and holding the left button for 500 ms inside an editable text
field starts recording. Releasing the button stops recording and starts
transcription, and the text is committed to the focused field as it is today.

If the pointer moves more than a few pixels before the threshold, the press
is treated as a normal click or text selection and nothing happens. If it
moves after recording started, the session is cancelled instead of committed,
because the application has most likely started a text selection that the
commit would replace.

### Capsule Position

The capsule, transient or pinned, is horizontally centered on the primary
screen with its bottom edge one eighth of the available height above the
screen's bottom edge. It never follows the caret: an earlier caret-anchored
placement covered the text being dictated into and jumped between fields.

## Detecting a Press Inside a Text Field

Fcitx addons only receive key and input-context events; mouse buttons never
reach them. Global button events come from deepin's
`org.deepin.dde.XEventMonitor1` session service (falling back to the legacy
`com.deepin.api.XEventMonitor`), which reports root-window button presses
without grabbing the pointer. This only works in X11 sessions, so long-press
is disabled unless the UI host runs on the `xcb` platform or deepin's `dxcb`
wrapper around it.

When the hold threshold elapses, the press qualifies if:

- an input context is focused and reports a caret rectangle;
- the press is not above the caret line and at most 400 logical pixels below
  it. Evaluating after the threshold means a caret the click itself moved is
  already current, and the empty area below the last line of a chat box still
  counts as part of the field;
- the top-level window under the pointer is the active window
  (`_NET_ACTIVE_WINDOW`), so the dock and other surfaces that never take
  input focus cannot trigger recording.

Password fields never report a caret, so they never qualify.

## Electron and Chromium Fields

When focus moves between inputs of one Chromium window on X11, Chromium
blurs and refocuses its input context several times per click but keeps
reporting the previous field's caret rectangle. It only sends the real caret
once the user types, and may echo the stale value again afterwards. This is
why an earlier focus-following hint stayed in place in Electron apps.

The UI host therefore treats a focus-in that arrives within 400 ms of a left
press (in either order, since D-Bus and Fcitx race) as caused by that click.
When the reported caret lies close to that press — on the caret's line, in
the field's padding, or in the empty space below the last text line — the
click itself moved the caret, so the report is fresh and the hint anchors at
it. Only when the report is far from the press does the hint, capsule, and
long-press check anchor at the press point, which is guaranteed to be inside
the newly focused field. A later caret report that differs from the
focus-time value replaces the press anchor; echoes of the stale value are
ignored. Keyboard focus changes have no press and keep using the reported
caret.

Hint updates are coalesced over 30 ms so the per-click blur/focus churn does
not make the hint flicker.

## Component Ownership

- **Fcitx addon** reports `FOCUS x y w h` on a program's first focus-in and
  `REFOCUS x y w h` on later ones, and `CARET x y w h` on caret changes,
  omitting geometry for password fields. It tracks the live input contexts
  per program so a program whose contexts have all gone away regains hint
  eligibility. It keeps sending `TYPED` and `BLUR`, and sends `START` on
  right `Ctrl` press and `STOP` on its release.
- **Service** forwards `FOCUS`, `REFOCUS`, `CARET`, `TYPED`, and `BLUR` to
  the UI socket so the UI host has a single input stream, and adds explicit
  `START` and `STOP` commands. `START` only starts from idle and `STOP` only
  finalizes a recording, so a hold cannot accidentally toggle a session in
  another phase.
- **UI host** owns presentation and mouse handling:
  - `CapsuleController` tracks focus and caret, decides hint visibility, and
    converts native caret pixels to Qt logical coordinates;
  - `LongPressDetector` is a pure state machine fed with presses, releases,
    pointer positions, and caret updates, emitting started, finished, and
    cancelled signals that map to `START`, `STOP`, and `CANCEL`;
  - `XEventMonitorSource` adapts the deepin D-Bus service to the detector;
  - `X11ActiveWindow` compares the window under the pointer with the active
    window through xcb.

## Settings

The `基础设置 → 交互` group adds two checkboxes, both enabled by default:

- `在输入框中长按鼠标左键说话` (`basic.interaction.long_press_to_talk`)
- `输入框获得焦点时显示语音提示` (`basic.interaction.focus_hint`)

Both apply live in the UI host and are ignored by the service.

## Testing

- `test_voice_session` covers forwarding of focus messages and the
  `START`/`STOP` semantics in every phase.
- `test_capsule_controller` covers hint visibility rules, timeout, the fixed
  capsule position, press anchoring for click-focused fields, stale caret echoes,
  focus churn, and persistent-mode precedence.
- `test_long_press_detector` covers the hold threshold, movement
  cancellation, caret qualification, the active-window check, and phase
  gating with fake inputs.

Manual verification requires an X11 deepin session and `fcitx5 -rd` after
installing the addon.
