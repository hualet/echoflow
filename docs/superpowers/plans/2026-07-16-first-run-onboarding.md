# EchoFlow First-Run Onboarding Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a reusable four-page first-run guide that activates from the application menu, downloads the default model, prepares EchoFlow services and Fcitx, resumes failures, and opens settings after completion.

**Architecture:** Keep `echoflow-ui` as the single desktop host. Add focused state, activation-IPC, asynchronous setup-controller, and dialog components; reuse `ModelDownloadCoordinator`, and route desktop, settings, and tray activation through `main.cpp`. Persist only a versioned completion marker and keep every external operation behind test seams.

**Tech Stack:** C++17, Qt 6 Core/Network/Widgets/Test, DTK 6 Widget/Core/Gui, QSettings, QLocalServer/QLocalSocket, QProcess, CMake/CTest.

---

## File Map

- `ui-host/OnboardingState.{h,cpp}`: versioned onboarding completion persistence.
- `ui-host/UiActivationServer.{h,cpp}`: primary-instance acquisition and `ACTIVATE` delivery.
- `ui-host/SetupCommandRunner.{h,cpp}`: asynchronous, injectable process execution.
- `ui-host/OnboardingSetupController.{h,cpp}`: final-page state machine and completion gate.
- `ui-host/OnboardingDialog.{h,cpp}`: four-page DTK-aware Qt Widgets presentation.
- `ui-host/SettingsDialog.{h,cpp}` and `ui-host/settings-schema.json`: reusable guide entry.
- `ui-host/main.cpp`: activation routing, shared dialog/controller lifetime, and tray action.
- `ui-host/echoflow.desktop.in` and `ui-host/CMakeLists.txt`: application entry and icon installation.
- `tests/test_onboarding_state.cpp`: state-file behavior.
- `tests/test_ui_activation_server.cpp`: live peer, stale socket, and message delivery.
- `tests/test_onboarding_setup_controller.cpp`: setup state machine with fakes.
- `tests/test_onboarding_dialog.cpp`: offscreen navigation and action-state tests.
- `tests/CMakeLists.txt` and `tests/spec/run_spec.sh`: build and installation coverage.

### Task 1: Versioned onboarding state

**Files:**
- Create: `ui-host/OnboardingState.h`
- Create: `ui-host/OnboardingState.cpp`
- Create: `tests/test_onboarding_state.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing state tests**

Cover an absent file, a lower version, current version, a successful completion
write, and a write failure. Use `QTemporaryDir` and an injected path:

```cpp
void TestOnboardingState::recordsCurrentVersion()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath("ui-state.ini"));
    QVERIFY(!state.isComplete());
    QString error;
    QVERIFY2(state.markComplete(&error), qPrintable(error));
    QVERIFY(OnboardingState(dir.filePath("ui-state.ini")).isComplete());
}
```

- [ ] **Step 2: Add the test target and prove RED**

Add a dedicated target linked to `Qt6::Core` and `Qt6::Test`, then run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target test_onboarding_state -j2
```

Expected: compilation fails because `OnboardingState` does not exist.

- [ ] **Step 3: Implement the minimal state class**

Expose this contract:

```cpp
class OnboardingState {
public:
    static constexpr int kCurrentVersion = 1;
    explicit OnboardingState(QString path = defaultPath());
    bool isComplete() const;
    bool markComplete(QString *error = nullptr);
    QString path() const;
    static QString defaultPath();
private:
    QString path_;
};
```

Use `QStandardPaths::ConfigLocation + "/echoflow/ui-state.ini"`, create the
parent directory, write `onboarding/version`, call `sync()`, and treat any
`QSettings::status()` other than `NoError` as failure.

- [ ] **Step 4: Run the focused test**

```bash
cmake --build build --target test_onboarding_state -j2
ctest --test-dir build -R '^test_onboarding_state$' --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit the state component**

```bash
git add ui-host/OnboardingState.h ui-host/OnboardingState.cpp \
  tests/test_onboarding_state.cpp tests/CMakeLists.txt
git commit -m "feat(onboarding): persist completion version" \
  -m "Keep first-run UI state separate from service configuration so explicit activation can reliably choose between the guide and settings."
```

### Task 2: Activation-aware single instance

**Files:**
- Create: `ui-host/UiActivationServer.h`
- Create: `ui-host/UiActivationServer.cpp`
- Create: `tests/test_ui_activation_server.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing activation tests**

Test that the first server becomes primary, a second client sends `ACTIVATE`,
the primary emits `activateRequested`, and a filesystem socket left by a dead
server is replaced. The public contract is:

```cpp
class UiActivationServer : public QObject {
    Q_OBJECT
public:
    enum class Result { Primary, ActivatedExisting, Failed };
    explicit UiActivationServer(QString socketPath, QObject *parent = nullptr);
    Result acquire(bool requestActivation, QString *error = nullptr);
signals:
    void activateRequested();
};
```

- [ ] **Step 2: Add the test target and prove RED**

Link `Qt6::Core`, `Qt6::Network`, and `Qt6::Test` and run:

```bash
cmake --build build --target test_ui_activation_server -j2
```

Expected: compilation fails because the activation server is missing.

- [ ] **Step 3: Implement safe acquisition and delivery**

Use a user-only `QLocalServer`. On listen failure, first connect to the socket.
If the peer is live, optionally write `ACTIVATE\n`, wait for bytes to flush, and
return `ActivatedExisting`. Remove the server path only after that connection
fails, then listen again. Connect `newConnection` to drain pending sockets and
emit once for each complete `ACTIVATE` line.

- [ ] **Step 4: Run focused tests**

```bash
cmake --build build --target test_ui_activation_server -j2
ctest --test-dir build -R '^test_ui_activation_server$' --output-on-failure
```

Expected: PASS without a desktop session.

- [ ] **Step 5: Commit activation IPC**

```bash
git add ui-host/UiActivationServer.h ui-host/UiActivationServer.cpp \
  tests/test_ui_activation_server.cpp tests/CMakeLists.txt
git commit -m "feat(ui): forward application activation" \
  -m "Let application-menu launches raise the existing tray host while preserving stale-socket recovery and single-instance safety."
```

### Task 3: Asynchronous setup controller

**Files:**
- Create: `ui-host/SetupCommandRunner.h`
- Create: `ui-host/SetupCommandRunner.cpp`
- Create: `ui-host/OnboardingSetupController.h`
- Create: `ui-host/OnboardingSetupController.cpp`
- Create: `tests/test_onboarding_setup_controller.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing controller tests with fakes**

Define item and aggregate behavior in tests:

```cpp
enum class SetupItem { Model, UiAutostart, Service, Fcitx };
enum class SetupItemState { Pending, Running, Succeeded, Failed };

void TestOnboardingSetupController::doesNotCompleteAfterPartialFailure()
{
    // Fake model starts incomplete; fake commands finish all but Fcitx.
    controller.start();
    model.finish(true, {});
    runner.finish("ui-autostart", true, {});
    runner.finish("service", true, {});
    runner.finish("fcitx", false, "fcitx5 exited with status 1");
    QVERIFY(!controller.isComplete());
    QVERIFY(!state.isComplete());
    QCOMPARE(controller.itemState(SetupItem::Fcitx), SetupItemState::Failed);
}
```

Also cover all-success completion, retrying only failed items, ignoring duplicate
Start, active model-download reconstruction, cancel as failure, and state-file
write failure. Command tests must require `systemctl is-enabled` after the UI
enable command and `systemctl is-active` after the service start command before
those items become Succeeded.

- [ ] **Step 2: Add test seams and prove RED**

Use small QObject interfaces in `OnboardingSetupController.h`:

```cpp
class ModelSetupSource : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual bool modelPresent() const = 0;
    virtual bool downloadRunning() const = 0;
    virtual void startDownload() = 0;
signals:
    void progress(qint64 done, qint64 total);
    void finished(bool ok, const QString &error);
};

class SetupCommandRunner : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void run(const QString &id, const QString &program,
                     const QStringList &arguments) = 0;
signals:
    void finished(const QString &id, bool ok, const QString &error);
};
```

Run the new target and expect missing implementation failures.

- [ ] **Step 3: Implement `QProcessSetupCommandRunner`**

Own one `QProcess` per command ID, reject a duplicate running ID, capture
stderr, and emit failure for `FailedToStart`, crashes, and nonzero exit. Emit
success only for normal exit code zero. Delete terminal processes with
`deleteLater()`.

- [ ] **Step 4: Implement the controller state machine**

The production commands must be exact:

```cpp
runner_->run("ui-autostart", "systemctl",
             {"--user", "enable", "echoflow-ui.service"});
runner_->run("service", "systemctl",
             {"--user", "enable", "--now", "echoflow.service"});
runner_->run("fcitx", "fcitx5", {"-rd"});
```

Start or observe the model through `ModelSetupSource`. Preserve succeeded items
on retry. After successful setup commands, run the exact probes
`systemctl --user is-enabled echoflow-ui.service` and
`systemctl --user is-active echoflow.service`; command success without a
successful probe is a failed item. When every item succeeds, call
`OnboardingState::markComplete`; expose state persistence failure as a failed
aggregate error and do not emit `setupComplete`.

- [ ] **Step 5: Run focused tests**

```bash
cmake --build build --target test_onboarding_setup_controller -j2
ctest --test-dir build -R '^test_onboarding_setup_controller$' --output-on-failure
```

Expected: PASS with no real commands or network calls.

- [ ] **Step 6: Commit controller and runner**

```bash
git add ui-host/SetupCommandRunner.h ui-host/SetupCommandRunner.cpp \
  ui-host/OnboardingSetupController.h ui-host/OnboardingSetupController.cpp \
  tests/test_onboarding_setup_controller.cpp tests/CMakeLists.txt
git commit -m "feat(onboarding): orchestrate first-run setup" \
  -m "Coordinate model, service, autostart, and Fcitx readiness behind injectable asynchronous boundaries so failed setup can resume safely."
```

### Task 4: Bridge the existing model coordinator

**Files:**
- Create: `ui-host/ModelSetupAdapter.h`
- Create: `ui-host/ModelSetupAdapter.cpp`
- Modify: `tests/test_onboarding_setup_controller.cpp`

- [ ] **Step 1: Add a failing adapter contract test**

Verify the adapter reports an already-present 0.6B model, maps a retained
Downloading snapshot to `downloadRunning()`, and filters coordinator signals to
the default model ID. Use this explicit constructor seam:

```cpp
using SnapshotProvider =
    std::function<DownloadSnapshot(const QString &modelId)>;
using StartDownload =
    std::function<void(const ModelEntry &, const QString &, const QString &)>;

ModelSetupAdapter(QString configDir, QString mirror,
                  SnapshotProvider snapshotProvider,
                  StartDownload startDownload,
                  QObject *parent = nullptr);
void observeCoordinator(ModelDownloadCoordinator *coordinator);
```

The production factory supplies lambdas that call the singleton's `snapshot`
and `start`; tests supply deterministic lambdas and invoke the adapter's
coordinator slots through `QMetaObject::invokeMethod` to verify ID filtering.

- [ ] **Step 2: Prove the adapter test fails**

```bash
cmake --build build --target test_onboarding_setup_controller -j2
```

Expected: compilation fails because `ModelSetupAdapter` is absent.

- [ ] **Step 3: Implement the adapter**

Resolve `qwen3-asr-0.6b` only through `ModelCatalog.h`. Determine presence with
`isModelPresent`, map the configured mirror to `https://hf-mirror.com` or
`https://huggingface.co`, and call `ModelDownloadCoordinator::start`. Forward
only the default model's progress and terminal state. Production construction
passes singleton-backed lambdas and calls `observeCoordinator`; no change to
`ModelDownloadCoordinator` ownership or singleton visibility is required.

- [ ] **Step 4: Run focused model/controller tests**

```bash
cmake --build build --target test_onboarding_setup_controller -j2
ctest --test-dir build -R '^test_onboarding_setup_controller$' --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit the production model bridge**

```bash
git add ui-host/ModelSetupAdapter.h ui-host/ModelSetupAdapter.cpp \
  tests/test_onboarding_setup_controller.cpp
git commit -m "feat(onboarding): reuse model download state" \
  -m "Bridge first-run setup to the tray-lifetime download coordinator so closing and reopening the guide preserves active progress."
```

### Task 5: Four-page onboarding dialog

**Files:**
- Create: `ui-host/OnboardingDialog.h`
- Create: `ui-host/OnboardingDialog.cpp`
- Create: `tests/test_onboarding_dialog.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing offscreen UI tests**

Give navigation and setup widgets stable object names. Verify four pages,
Back/Next bounds, final-page Start, progress labels, failed Retry, successful
Finish, Escape/close without completion, and `showForIncompleteSetup()` opening
the final page when work has already started.

```cpp
QCOMPARE(dialog.findChild<QStackedWidget *>("pages")->count(), 4);
QTest::mouseClick(dialog.findChild<QPushButton *>("nextButton"), Qt::LeftButton);
QCOMPARE(dialog.currentPage(), 1);
```

- [ ] **Step 2: Add the widget target and prove RED**

Link Qt Widgets/Test and DTK Widget/Gui. Set its CTest environment to
`QT_QPA_PLATFORM=offscreen`. Expected: missing dialog compilation failure.

- [ ] **Step 3: Implement the dialog shell and content**

Build focused helper methods `createIntroPage`, `createShortcutPage`,
`createSettingsPage`, and `createSetupPage`. Use standard labels, application
icon, palette roles, word wrap, accessible names, and Back/Next/Page indicator.
Do not embed setup or process logic in the widget.

- [ ] **Step 4: Bind controller state**

Render the four setup items from controller signals. Start invokes
`controller->start()`, Retry invokes `controller->retryFailed()`, and Finish
emits `finishedAndSettingsRequested`. Closing only hides/destroys the dialog;
it never calls `markComplete`.

- [ ] **Step 5: Run focused UI tests**

```bash
cmake --build build --target test_onboarding_dialog -j2
QT_QPA_PLATFORM=offscreen ctest --test-dir build \
  -R '^test_onboarding_dialog$' --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit the dialog**

```bash
git add ui-host/OnboardingDialog.h ui-host/OnboardingDialog.cpp \
  tests/test_onboarding_dialog.cpp tests/CMakeLists.txt
git commit -m "feat(onboarding): add first-run guide dialog" \
  -m "Present the approved four-step workflow and bind recoverable setup progress without coupling the UI to network or process execution."
```

### Task 6: Settings, tray, and activation integration

**Files:**
- Modify: `ui-host/settings-schema.json`
- Modify: `ui-host/SettingsDialog.h`
- Modify: `ui-host/SettingsDialog.cpp`
- Modify: `ui-host/main.cpp`
- Modify: `ui-host/CMakeLists.txt`
- Modify: `tests/spec/run_spec.sh`

- [ ] **Step 1: Add failing spec assertions**

Require the new sources in `ui-host/CMakeLists.txt`, `--activate` parsing and
`UiActivationServer` in `main.cpp`, the tray Usage guide action, a `guide`
custom settings type, and `usageGuideRequested` in `SettingsDialog`.

- [ ] **Step 2: Run the spec test and prove RED**

```bash
bash tests/spec/run_spec.sh
```

Expected: the new onboarding integration assertions fail.

- [ ] **Step 3: Add the settings guide row**

Insert a top-level Getting started group before Basic, containing one custom
`guide` option. Register its factory before `updateSettings()`, return a left
label and right `QPushButton`, and emit `usageGuideRequested` on click.

- [ ] **Step 4: Integrate activation and shared dialog lifetime in `main.cpp`**

Parse `--activate` before instance acquisition. Replace the inline
`acquireUiInstanceServer` flow with `UiActivationServer`. After tray/settings
initialization, define shared `openSettings` and `showOnboarding(bool replay)`
lambdas. Route:

```text
ACTIVATE + incomplete -> showOnboarding(false)
ACTIVATE + complete   -> openSettings()
tray/settings guide   -> showOnboarding(true)
finish onboarding     -> close guide, openSettings()
```

Keep the setup controller, model adapter, runner, and state alive for the main
process lifetime. Preserve QML tooltip, socket, settings-sync, and service
restart behavior.

- [ ] **Step 5: Build and run focused integration checks**

```bash
cmake --build build --target echoflow-ui -j2
bash tests/spec/run_spec.sh
QT_QPA_PLATFORM=offscreen ctest --test-dir build \
  -R 'test_onboarding|test_ui_activation|spec_tests' --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit UI integration**

```bash
git add ui-host/main.cpp ui-host/SettingsDialog.h ui-host/SettingsDialog.cpp \
  ui-host/settings-schema.json ui-host/CMakeLists.txt tests/spec/run_spec.sh
git commit -m "feat(ui): connect onboarding entry points" \
  -m "Route application, tray, and settings activations through one reusable guide while later application launches continue directly to settings."
```

### Task 7: Desktop entry and installation

**Files:**
- Create: `ui-host/echoflow.desktop.in`
- Modify: `ui-host/CMakeLists.txt`
- Modify: `tests/spec/run_spec.sh`

- [ ] **Step 1: Add failing installation assertions**

Check that the desktop template contains `Exec=echoflow-ui --activate`,
`Icon=echoflow`, `Type=Application`, and that CMake installs both the configured
desktop file and `icons/echoflow.svg` to standard GNU data directories.

- [ ] **Step 2: Prove RED**

```bash
bash tests/spec/run_spec.sh
```

Expected: desktop-entry assertions fail.

- [ ] **Step 3: Add and install the desktop entry**

Use:

```ini
[Desktop Entry]
Type=Application
Name=EchoFlow
Comment=Offline voice input for Fcitx5
Exec=echoflow-ui --activate
Icon=echoflow
Terminal=false
Categories=Utility;Accessibility;
```

Configure it into the build tree and install it under
`${CMAKE_INSTALL_DATADIR}/applications`; install the SVG under
`${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps`.

- [ ] **Step 4: Verify an isolated install tree**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target echoflow-ui -j2
DESTDIR="$PWD/build/install-root" cmake --install build
test -f build/install-root/usr/share/applications/echoflow.desktop
test -f build/install-root/usr/share/icons/hicolor/scalable/apps/echoflow.svg
```

Expected: both files exist; adapt the inspected prefix if CMake uses
`/usr/local` in the local build.

- [ ] **Step 5: Commit installation plumbing**

```bash
git add ui-host/echoflow.desktop.in ui-host/CMakeLists.txt tests/spec/run_spec.sh
git commit -m "feat(packaging): install EchoFlow launcher" \
  -m "Expose an application-menu activation path and install the scalable icon so first-run onboarding is reachable in native and local installs."
```

### Task 8: Full verification and runtime handoff

**Files:**
- Modify only if verification exposes an onboarding defect.

- [ ] **Step 1: Run formatting and static repository checks**

```bash
git diff --check
bash -n install-user.sh uninstall-user.sh tests/spec/*.sh
sh -n run.sh
```

Expected: no output from syntax/diff checks.

- [ ] **Step 2: Run the complete build and automated suite**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j2
ctest --test-dir build --output-on-failure
./build/service/echoflow-service --print-default-config
./build/service/echoflow-service --self-test
```

Expected: build succeeds, every CTest passes, and both service sanity commands
return zero.

- [ ] **Step 3: Inspect the final diff and commit any verification-only fix**

```bash
git status --short
git diff --stat HEAD~7..HEAD
git log --oneline --decorate -10
```

Stage only intended source, test, desktop, and documentation files. Never stage
`.superpowers/`, build output, model weights, recordings, or installed files.

- [ ] **Step 4: Manual desktop verification**

Install with `./install-user.sh`, restart Fcitx if needed, and verify the exact
acceptance path: silent service start, first menu activation, four pages,
download close/reopen recovery, failure retry, completion to settings, later
activation to settings, both replay entries, right Ctrl dictation, and
tray/service availability after a new login. Record any environment-specific
step in the final handoff rather than weakening automated assertions.
