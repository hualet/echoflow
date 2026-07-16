# EchoFlow First-Run Onboarding Design

Date: 2026-07-16
Status: approved

## Problem

EchoFlow currently installs a tray UI, settings dialog, background service, and
model download controls, but it does not provide an installation-first setup
flow. There is no application-menu desktop entry, no persisted onboarding
state, and no reusable guide. When `echoflow-ui` is already running, launching
a second instance exits without asking the existing instance to show anything.

Users need a guided first launch that explains the product, teaches the right
Ctrl recording gesture, shows where settings live, and leaves the system in a
usable state with a model, services, and Fcitx ready.

## Goals

- Open a four-page onboarding slideshow when the user activates EchoFlow from
  the application menu for the first time.
- Keep service-started UI instances silent until the user explicitly activates
  EchoFlow.
- Reuse one onboarding window from first launch, the settings dialog, and the
  tray menu.
- Download the default 0.6B model and prepare the user services and Fcitx from
  the final page.
- Persist completion only after every required setup operation succeeds.
- Resume an incomplete or failed setup without discarding successful work.
- Open settings, rather than replaying onboarding, on later application-menu
  activations.

## Non-goals

- Replace the existing settings dialog or model download implementation.
- Download more than the default 0.6B model during onboarding.
- Add a general-purpose tutorial framework or remote onboarding content.
- Require model weights, network access, systemd, PipeWire, or a running Fcitx
  daemon in automated tests.
- Automatically replay an already completed onboarding after an application
  upgrade unless the onboarding version is intentionally increased.

## User Experience

The onboarding is an independent DTK/Qt Widgets dialog backed by a
`QStackedWidget`. It uses the application icon, follows the current DTK
palette, and provides Back and Next controls plus a page indicator. Closing the
window never exits the tray host.

The four pages are:

1. **Offline, private, and responsive.** Explain that transcription runs on
   the device, voice data does not need to leave it, and the interaction is
   designed for daily dictation.
2. **Press right Ctrl to speak.** Explain that the first press starts recording,
   the second press stops it, and the transcript is committed to the focused
   input field.
3. **Use the tray and settings.** Show where to change the model, language,
   microphone, and download mirror, and mention that the guide can be reopened.
4. **Prepare EchoFlow.** Show the default model download and separate Model,
   Background service, and Fcitx readiness rows.

The last page initially offers **Start using EchoFlow**. While setup runs, the
button becomes unavailable and the rows show progress. After all rows succeed,
the primary action becomes **Finish and open settings**. If one or more rows
fail, the page shows the concrete error and a **Retry** action that reruns only
failed work.

The settings schema gains a small top-level Getting started group before the
existing Basic group. Its custom **Usage guide** row opens the same onboarding
dialog. The tray menu receives a **Usage guide** action above Settings.

## Activation and Single-Instance Flow

A new installed `echoflow.desktop` entry executes:

```text
echoflow-ui --activate
```

`echoflow-ui` keeps one primary process. The existing local `QLocalServer`
instance endpoint becomes an activation channel:

- A duplicate `--activate` process connects, sends an `ACTIVATE` message, and
  exits successfully.
- The primary process accepts the message and raises the appropriate window.
- A `--activate` process that becomes the primary instance initializes the tray
  host and performs the same activation locally.
- A systemd-started process without `--activate` initializes the tray and
  sockets without showing onboarding or settings.

On explicit activation, incomplete onboarding opens the guide. Completed
onboarding opens settings. A user-requested Usage guide action always opens the
guide without clearing completion.

The activation command is handled only after settings, tray, and dialogs can be
created. Messages arriving during initialization are queued for delivery once
the UI is ready.

## Persisted State

UI-only state lives separately from the service configuration in
`~/.config/echoflow/ui-state.ini`, honoring `XDG_CONFIG_HOME` through
`QStandardPaths`. The file stores an integer onboarding version rather than a
boolean. Version `1` means this design has completed; a lower or absent version
means onboarding is incomplete.

Completion is written atomically through `QSettings::sync()` only after:

- every required file for `qwen3-asr-0.6b` is present;
- `echoflow.service` was enabled and is active;
- `echoflow-ui.service` was enabled for later logins; and
- the Fcitx reload command completed successfully.

Closing the window, canceling the model download, or encountering a failed
setup item does not change the stored version. Replaying an already completed
guide never clears it.

## Components

### `OnboardingState`

A small UI-independent class reads and writes the onboarding version using an
injected state-file path. It answers whether the current version is complete
and records completion. Tests use a temporary directory.

### `UiActivationServer`

This class owns the local server, stale-socket recovery, duplicate-instance
message delivery, and queued activation behavior. It exposes an activation
signal to `main.cpp`. DTK's single-instance guard remains a secondary safety
check, but the EchoFlow local server is the activation transport.

### `OnboardingSetupController`

This QObject coordinates, but does not render, final-page setup. It exposes
per-item state and aggregate readiness to the dialog. Its dependencies are:

- the existing `ModelDownloadCoordinator` for model progress, cancellation,
  and retained snapshots;
- an injected command runner for systemd and Fcitx operations; and
- `OnboardingState` for the final completion write.

The production command runner uses asynchronous `QProcess` calls so the UI
thread never blocks. Tests use a deterministic fake runner.

The environment operations are:

```text
systemctl --user enable echoflow-ui.service
systemctl --user enable --now echoflow.service
fcitx5 -rd
```

The currently running tray process remains alive when launched directly from
the desktop entry. Enabling its systemd unit prepares later logins without
trying to replace the current process.

### `OnboardingDialog`

The dialog owns page navigation and renders controller state. It does not own
downloaders or child setup processes. Reconstructing the dialog queries current
model files, coordinator snapshots, and service readiness, so closing and
reopening it resumes the final page accurately.

When incomplete setup already has work in progress or failed work, explicit
activation opens the final page. Otherwise it opens page one. Replaying a
completed guide opens page one and treats the final page as already ready.

### Settings and Tray Integration

`SettingsDialog` registers a custom guide-row factory before building settings
widgets and emits a `usageGuideRequested` signal when its button is pressed.
`main.cpp` routes both this signal and the tray action to one
`showOnboarding()` lambda. The existing settings lifecycle and service restart
after settings changes remain unchanged.

## Setup State Machine

Each setup item is `Pending`, `Running`, `Succeeded`, or `Failed`. The model
item additionally exposes byte progress when available.

When setup starts:

- work already proven complete becomes `Succeeded` without rerunning;
- an active coordinator download remains `Running` and is observed;
- missing model files start the existing coordinator download;
- service and Fcitx commands start asynchronously; and
- duplicate Start or Retry actions are ignored while work is running.

Retry changes only failed items back to `Running`. Successful items are
preserved. A canceled model download is a failed model item and can be retried.
Completion is recorded once, when all required items are `Succeeded`. The
dialog then enables Finish and open settings.

## Error Handling

- Model HTTP, filesystem, and cancellation errors use the existing downloader's
  concrete error message.
- Process startup failure, timeout, nonzero exit, and stderr are converted into
  concise per-item messages. No command is retried automatically in a loop.
- Failure to persist the completion version is presented as a setup failure;
  the UI does not claim onboarding is complete.
- A missing `fcitx5` executable produces an actionable message rather than
  silently succeeding.
- An activation connection failure returns a nonzero exit from the duplicate
  process and logs the socket error.
- A stale activation socket is removed only after a connection attempt proves
  there is no live peer.

## Installation

CMake installs the desktop entry under `${CMAKE_INSTALL_DATADIR}/applications`
with the EchoFlow icon and an appropriate Voice/Utility category. Both native
Debian packaging and `install-user.sh` inherit the CMake installation. The
desktop entry does not start setup commands itself; all setup work stays in the
UI controller, where errors can be shown and retried.

## Testing

Automated tests do not touch the real user configuration or desktop services.
They cover:

- absent, older, current, and failed-to-sync onboarding state;
- explicit activation before and after completion;
- duplicate-instance `ACTIVATE` delivery and stale-socket recovery;
- four-page navigation and final-page button state under the offscreen Qt
  platform;
- fresh setup success, partial failure, retry, cancellation, dialog close and
  reopen, and completion written only after all required items succeed;
- reconstruction from an active model download snapshot;
- replaying a completed guide without redownloading or clearing completion;
  and
- spec assertions for the desktop entry, CMake installation, UI sources, tray
  action, and settings guide row.

The full verification path is:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
./build/service/echoflow-service --print-default-config
./build/service/echoflow-service --self-test
bash -n install-user.sh uninstall-user.sh tests/spec/*.sh
sh -n run.sh
```

Manual verification on a real desktop covers first menu activation, completed
menu activation, both replay entry points, download resume after closing the
dialog, failure and retry messaging, right Ctrl dictation, and tray/service
availability after logging out and back in.

## Acceptance Criteria

- Installing EchoFlow creates a visible application-menu entry.
- The first explicit activation opens onboarding; service startup alone does
  not open a window.
- The confirmed four pages are present and usable with keyboard and mouse.
- Start using EchoFlow downloads the default model and prepares the services
  and Fcitx while showing recoverable status.
- Closing or failing setup cannot mark onboarding complete.
- Successful setup survives process and login restarts.
- Later application-menu activation opens settings.
- Settings and tray actions reopen the reusable guide.
- The complete automated suite passes without external services, network, or
  model weights.
