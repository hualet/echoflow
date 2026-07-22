# Persistent Recording Capsule Design

Date: 2026-07-22

## Goal

Provide a mouse-driven voice-input path for machines without a right Ctrl key.
The tray menu can pin the existing recording capsule on screen, and the capsule
can then start and stop voice input without requiring a keyboard shortcut.

## User Experience

The tray menu adds one checkable action named `显示录音胶囊` before the existing
usage-guide and settings actions.

- Checking the action enters persistent mode and immediately shows an idle
  capsule at the existing bottom-center position.
- Unchecking it exits persistent mode and hides the capsule.
- Persistent mode is process-local. It is disabled when the UI host restarts
  and is not written to the user configuration.

The idle capsule reuses the current right-hand circular action button. Its icon
is a microphone, and clicking it starts recording. The center retains a quiet,
non-animated waveform so the capsule remains recognizable as the voice-input
control. The idle state has no left cancel button because the tray action owns
closing the persistent capsule.

While recording, the capsule keeps its existing layout and behavior: the left
X discards the current recording, the waveform animates, and the right check
button stops recording and begins transcription. In persistent mode, using the
X returns to the idle capsule instead of hiding it.

During transcription, the capsule shows `正在转写` as it does today. Successful,
empty, and failed sessions all return a persistent capsule to its idle state.
Without persistent mode, the current right-Ctrl workflow remains transient and
hides the capsule when the session returns to idle.

## Cancellation Semantics

Unchecking `显示录音胶囊` is a session-cancel operation as well as a visibility
change:

- while idle, it only hides the capsule;
- while recording, it stops capture and discards all audio and partial text;
- while transcribing, it hides immediately and marks the in-flight session as
  cancelled, so its eventual recognition result must never be committed.

The same discard rule applies if persistent mode is closed while the service is
finishing the live pipeline. The underlying model call may finish naturally,
because forcibly interrupting model-owned state is unsafe, but cancellation
must prevent all later text commit and UI resurrection.

If the user reopens the persistent capsule before cancelled background work has
finished, the capsule shows `正在取消` and does not offer the microphone button.
It returns to idle as soon as cleanup completes. This prevents a second session
from reusing ASR pipeline state that is still active.

## State Ownership

The UI host owns presentation mode, and the service owns voice-session state.
They remain separate so keyboard activation does not need a new Fcitx protocol.

### UI Presentation State

A focused UI-host controller owns:

- `persistent`: whether the tray action is checked;
- the most recent service phase: idle, recording, transcribing, or cancelling;
- the rule that idle is visible only in persistent mode;
- the rule that recording and transcribing are visible in either mode.

The controller, rather than `main.cpp` or QML, decides the resulting capsule
presentation. It emits one view state to QML containing visibility, phase,
message, and the existing fixed position. This makes the transient/persistent
rules testable without loading the QML scene.

The service continues to emit session messages through the existing UI socket:

```text
IDLE
RECORDING
STREAM_TEXT <text>
TRANSCRIBING
CANCELLING
```

The tray controller sends `STATUS` when persistent mode is enabled. The service
responds by emitting its current phase on the UI socket. This covers reopening
the capsule while cancelled transcription is still winding down. `STATUS` is
read-only and does not change the voice session.

### Service Session State

Stopping a recording no longer performs final transcription and commit inside
the control-server receive loop. Instead it performs the short capture-stop
transition, records a monotonically increasing session generation, and runs
final transcription in one owned background task. The control loop therefore
remains able to receive `CANCEL` and `STATUS`.

Session state and cancellation metadata are protected by a mutex. Slow capture,
ASR, and commit calls run without holding that mutex. Before committing, the
background task reacquires the state and verifies that its generation is still
current and has not been cancelled. A cancelled or superseded generation can
never call `ICommitter::commitText`.

Only one finalization task may exist at a time. A new start request received
while a cancelled task is still cleaning up returns a busy/cancelling result
and does not touch the pipeline. When cleanup finishes, the task changes the
service to idle and emits `IDLE`; the UI controller either shows the persistent
idle capsule or leaves a transient capsule hidden.

`VoiceSession` owns and joins the background task during destruction. Shutdown
must not leave callbacks referencing destroyed pipeline, committer, or notifier
objects.

## Control Flow

### Persistent session

```text
tray checked -> UI persistent=true -> STATUS -> IDLE -> idle capsule
mic clicked  -> CTRL_DOWN -> RECORDING -> recording capsule
check clicked -> CTRL_DOWN -> TRANSCRIBING -> background finalize
final result -> commit if generation is current -> IDLE -> idle capsule
tray unchecked -> UI persistent=false -> CANCEL -> capsule hidden
```

### Transient session

```text
right Ctrl -> CTRL_DOWN -> RECORDING -> recording capsule
right Ctrl/check -> CTRL_DOWN -> TRANSCRIBING -> background finalize
final result -> commit if not cancelled -> IDLE -> capsule hidden
```

### Close during transcription

```text
tray unchecked -> UI hides immediately -> CANCEL marks generation cancelled
background finalize returns -> result discarded -> IDLE remains hidden
```

## Error Handling

Failure to send a tray or capsule command leaves the service unchanged. The UI
logs the failure; if persistent mode was being enabled, it still shows the idle
capsule so the entry remains available, and a failed start is corrected by the
service's existing `IDLE` notification when the command reaches a live service.

Recording-start and transcription exceptions return the service to idle. They
must preserve the presentation rule: persistent mode shows the idle capsule,
while transient mode hides it. Cancellation wins over an ASR error and never
commits partial or stale output.

Late `STREAM_TEXT` messages are accepted only for the current recording
generation. They are ignored after stop or cancellation, preserving the
existing protection against stale partial text.

## Testing

Service tests use the existing fake recorder, ASR, live pipeline, committer, and
UI notifier boundaries. Focused tests verify that:

- stopping returns promptly while a fake transcription is blocked;
- `CANCEL` is accepted during transcription;
- a result produced after cancellation is never committed;
- the finalization task emits idle after cancelled cleanup;
- start is rejected while cancelled finalization is still cleaning up;
- `STATUS` reports idle, recording, transcribing, and cancelling accurately;
- destruction safely joins an in-flight task;
- ordinary recording, live recording, empty text, error, and successful commit
  behavior remain intact.

UI controller tests verify the full presentation matrix:

- transient idle is hidden;
- persistent idle is visible with the microphone action;
- recording and transcription are visible in both modes;
- service idle after a persistent session returns to the idle capsule;
- service idle after a transient session hides it;
- closing persistent mode hides immediately and requests cancellation;
- reopening during cancellation shows `正在取消` until service idle.

QML/spec checks verify the idle microphone icon, enabled/disabled action states,
and preservation of the existing recording buttons. The full CTest and shell
spec suites remain required. Manual verification covers tray-only start, stop,
commit, recording cancellation, transcription cancellation, reopening during
cleanup, and the unchanged right-Ctrl transient path with the installed UI and
service.

## Scope Boundaries

This change does not add a configurable keyboard shortcut, persist capsule
visibility, forcefully interrupt model inference, permit overlapping sessions,
or change where committed text is delivered. It extends the existing capsule
and Unix-datagram protocols only as needed for persistent presentation and safe
cancellation.
