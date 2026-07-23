# Persistent Recording Capsule Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a tray-pinned recording capsule that starts and stops voice input by mouse while preserving the transient right-Ctrl workflow and safely discarding transcription when closed.

**Architecture:** Extract presentation state from `ui-host/main.cpp` into a testable `CapsuleController`. Make `VoiceSession` finalize recordings on one owned background thread so the datagram server can process `CANCEL` and `STATUS` during ASR; a generation-scoped cancellation gate prevents stale results from reaching the committer.

**Tech Stack:** C++17, Qt 6/QTest, Qt Quick QML, Unix datagram sockets, CMake/CTest.

---

## File Map

- Create `ui-host/CapsuleController.{h,cpp}` for persistent/transient UI state and service-message parsing.
- Create `tests/test_capsule_controller.cpp` for controller behavior without loading QML.
- Modify `ui-host/main.cpp`, `ui-host/CMakeLists.txt`, and `tests/CMakeLists.txt` for tray and build wiring.
- Modify `qml/EchoFlowTooltip.qml` for idle, recording, transcribing, and cancelling phases.
- Modify `service/VoiceSession.{h,cpp}` and `service/Server.cpp` for async finalization, cancellation, and `STATUS`.
- Modify `tests/test_voice_session.cpp`, `tests/benchmarks/voice_latency_benchmark.cpp`, and `tests/spec/run_spec.sh` for regression coverage.

### Task 1: Model Persistent and Transient Presentation

**Files:**
- Create: `ui-host/CapsuleController.h`
- Create: `ui-host/CapsuleController.cpp`
- Create: `tests/test_capsule_controller.cpp`
- Modify: `ui-host/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing controller tests**

Use an injected command sender and `QSignalSpy` on `capsuleChanged`:

```cpp
void TestCapsuleController::persistentIdleIsVisibleAndRequestsStatus()
{
    QStringList commands;
    CapsuleController controller([&](std::string_view command) {
        commands << QString::fromUtf8(command.data(), qsizetype(command.size()));
        return true;
    });
    QSignalSpy spy(&controller, &CapsuleController::capsuleChanged);
    controller.setPersistent(true);
    QCOMPARE(commands, QStringList{QStringLiteral("STATUS")});
    QCOMPARE(spy.last().at(0).toBool(), true);
    QCOMPARE(spy.last().at(1).toString(), QStringLiteral("idle"));
}

void TestCapsuleController::closingPersistentHidesAndCancels()
{
    QStringList commands;
    CapsuleController controller([&](std::string_view command) {
        commands << QString::fromUtf8(command.data(), qsizetype(command.size()));
        return true;
    });
    QSignalSpy spy(&controller, &CapsuleController::capsuleChanged);
    controller.setPersistent(true);
    spy.clear();
    controller.setPersistent(false);
    QCOMPARE(commands.last(), QStringLiteral("CANCEL"));
    QCOMPARE(spy.last().at(0).toBool(), false);
}
```

Add separate tests for transient `IDLE` hiding; persistent `IDLE` remaining
visible; `RECORDING`, `STREAM_TEXT`, `TRANSCRIBING`, and `CANCELLING`; late
stream text being ignored; `requestToggle()` sending `CTRL_DOWN`; and
`requestCancel()` preserving persistent mode while sending `CANCEL`.

- [ ] **Step 2: Register and run the test to verify RED**

Add `test_capsule_controller` to both branches of `tests/CMakeLists.txt`, compile
`../ui-host/CapsuleController.cpp`, and link `Qt6::Core`, `Qt6::Gui`, and
`Qt6::Test`.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target test_capsule_controller -j2
ctest --test-dir build -R '^test_capsule_controller$' --output-on-failure
```

Expected: compilation fails because `CapsuleController` does not exist.

- [ ] **Step 3: Implement the minimal controller**

Expose this boundary in `CapsuleController.h`:

```cpp
class CapsuleController final : public QObject {
    Q_OBJECT
public:
    using SendCommand = std::function<bool(std::string_view)>;
    explicit CapsuleController(SendCommand sendCommand, QObject *parent = nullptr);
    bool persistent() const { return persistent_; }
    void applyServiceMessage(const QString &message);
public slots:
    void setPersistent(bool persistent);
    void requestToggle();
    void requestCancel();
signals:
    void capsuleChanged(bool visible, const QString &phase,
                        const QString &message, bool hasPosition,
                        int moveX, int moveY);
    void persistentChanged(bool persistent);
private:
    enum class Phase { Idle, Recording, Transcribing, Cancelling };
    void publish();
    SendCommand sendCommand_;
    Phase phase_ = Phase::Idle;
    bool persistent_ = false;
    QString message_;
};
```

`setPersistent(true)` publishes the current phase and sends `STATUS`.
`setPersistent(false)` first changes the local phase to `Cancelling`, then
publishes hidden immediately and sends `CANCEL`. The service's following
`IDLE` notification settles an idle close; if the user reopens before it
arrives, the local cancelling phase prevents a false microphone affordance.
`applyServiceMessage()` maps service messages to stable phase strings and
Chinese labels. `publish()` uses the existing primary-screen bottom-center
anchor and the visibility rule:

```cpp
const bool visible = persistent_
    || phase_ == Phase::Recording
    || phase_ == Phase::Transcribing;
```

`Cancelling` is visible only if the user has reopened persistent mode.

- [ ] **Step 4: Verify GREEN and commit**

```bash
cmake --build build --target test_capsule_controller -j2
ctest --test-dir build -R '^test_capsule_controller$' --output-on-failure
git add ui-host/CapsuleController.h ui-host/CapsuleController.cpp \
  ui-host/CMakeLists.txt tests/test_capsule_controller.cpp tests/CMakeLists.txt \
  docs/superpowers/plans/2026-07-22-persistent-recording-capsule.md
git commit -m "feat(ui): model persistent capsule state" -m \
  "Keep tray persistence separate from service session state so keyboard and mouse activation share one capsule without changing Fcitx behavior."
```

### Task 2: Make Final Transcription Cancellable

**Files:**
- Modify: `service/VoiceSession.h`
- Modify: `service/VoiceSession.cpp`
- Modify: `tests/test_voice_session.cpp`

- [ ] **Step 1: Add blocking fakes and failing async tests**

Make fake UI/committer collections mutex-protected. Add a blocking live fake:

```cpp
struct BlockingLivePipeline : FakeLivePipeline {
    std::mutex mutex;
    std::condition_variable ready;
    bool finishEntered = false;
    bool releaseFinish = false;
    std::string finish() override {
        std::unique_lock<std::mutex> lock(mutex);
        ++finishes;
        finishEntered = true;
        ready.notify_all();
        ready.wait(lock, [&] { return releaseFinish; });
        return result;
    }
};
```

Test that the second `CTRL_DOWN` returns `TRANSCRIBING` while `finish()` is
blocked. Then test cancellation:

```cpp
QCOMPARE(QString::fromStdString(session.handleCommand("CANCEL")),
         QStringLiteral("CANCELLED"));
QCOMPARE(session.state(), SessionState::Cancelling);
pipeline.release();
QTRY_COMPARE(session.state(), SessionState::Idle);
QVERIFY(committer.snapshot().empty());
```

Add tests that `STATUS` reports every phase, start during cancelling returns
`CANCELLING`, exceptions and empty results end in idle, and destruction safely
joins after the fake is released.

- [ ] **Step 2: Run the focused test to verify RED**

```bash
cmake --build build --target test_voice_session -j2
ctest --test-dir build -R '^test_voice_session$' --output-on-failure
```

Expected: stop blocks, `STATUS` is unknown, and no cancelling state exists.

- [ ] **Step 3: Add owned async state to `VoiceSession`**

Extend `SessionState` with `Cancelling`, make `state()` lock the mutex, and add:

```cpp
~VoiceSession();
mutable std::mutex mutex_;
std::thread finalizeThread_;
uint64_t generation_ = 0;
bool cancelRequested_ = false;
bool finalizeRunning_ = false;
std::string beginFinalization();
void finalize(uint64_t generation);
void joinFinishedTask();
std::string status();
```

`beginFinalization()` sets `Transcribing`, emits `TRANSCRIBING`, starts exactly
one background worker, and returns immediately. The worker owns recorder
stop/live finish, punctuation stripping, and the final commit gate. Under the
mutex it verifies `generation == generation_ && !cancelRequested_` before
calling `commitText`; otherwise it discards text. It then sets idle and emits
`IDLE` after releasing the mutex.

During transcription, `CANCEL` sets the generation's cancel flag, changes the
phase to `Cancelling`, and emits `CANCELLING`. Do not call
`livePipeline_->cancel()` concurrently with `finish()`. During recording retain
the existing direct cancel path. Join a completed worker before starting a new
session; the destructor cancels active work and joins before collaborators can
be destroyed.

- [ ] **Step 4: Convert existing synchronous expectations**

Successful, empty, and exception tests now expect `TRANSCRIBING`, then use
`QTRY_COMPARE(session.state(), SessionState::Idle)` before inspecting committed
text and UI messages. Keep start, recording cancel, blur, partial-text, and
unknown-command assertions synchronous.

- [ ] **Step 5: Verify repeatedly and commit**

```bash
cmake --build build --target test_voice_session -j2
for i in 1 2 3 4 5; do
  ctest --test-dir build -R '^test_voice_session$' --output-on-failure || exit 1
done
git add service/VoiceSession.h service/VoiceSession.cpp tests/test_voice_session.cpp
git commit -m "feat(service): cancel in-flight transcription" -m \
  "Move final transcription off the control loop and gate commits by session generation so closing the capsule can discard work that is already being decoded."
```

### Task 3: Admit Read-Only Status Queries

**Files:**
- Modify: `service/Server.cpp`
- Modify: `tests/spec/run_spec.sh`

- [ ] **Step 1: Add and run a failing protocol spec**

```bash
for verb in FOCUS BLUR CTRL_DOWN TYPED CANCEL STATUS; do
  assert_contains "$ROOT/service/VoiceSession.cpp" "$verb" "verb $verb handled"
done
assert_contains "$ROOT/service/Server.cpp" '"STATUS"' "server admits status queries"
bash tests/spec/run_spec.sh
```

Expected: FAIL at `server admits status queries`.

- [ ] **Step 2: Add `STATUS` to the allow-list**

```cpp
constexpr std::array<const char*, 6> kAllowedCommands = {
    "FOCUS", "BLUR", "CTRL_DOWN", "TYPED", "CANCEL", "STATUS"
};
```

- [ ] **Step 3: Verify and commit**

```bash
bash tests/spec/run_spec.sh
git add service/Server.cpp tests/spec/run_spec.sh
git commit -m "feat(service): expose voice session status" -m \
  "Let a newly pinned capsule recover the service phase, including cancellation cleanup, without changing the active recording."
```

### Task 4: Render Idle and Cancelling States

**Files:**
- Modify: `qml/EchoFlowTooltip.qml`
- Modify: `tests/spec/run_spec.sh`

- [ ] **Step 1: Add and run failing QML checks**

```bash
assert_contains "$ROOT/qml/EchoFlowTooltip.qml" 'property string phase: "idle"' "capsule exposes explicit phase"
assert_contains "$ROOT/qml/EchoFlowTooltip.qml" 'id: microphoneButton' "idle capsule has microphone action"
assert_contains "$ROOT/qml/EchoFlowTooltip.qml" 'root.phase === "recording"' "wave animation is recording-only"
assert_contains "$ROOT/qml/EchoFlowTooltip.qml" 'root.phase === "cancelling"' "capsule renders cancellation cleanup"
bash tests/spec/run_spec.sh
```

Expected: the new assertions fail.

- [ ] **Step 2: Add explicit phase rendering**

Use the controller's signal shape and stop inferring phase from message text:

```qml
property string phase: "idle"
readonly property bool idle: phase === "idle"
readonly property bool recording: phase === "recording"
readonly property bool transcribing: phase === "transcribing"
readonly property bool cancelling: phase === "cancelling"
```

Show a quiet non-animated waveform and right-side `microphoneButton` in idle.
The microphone Canvas draws a body, stem, and stand and calls
`tooltipController.requestToggle()`. Keep X/check buttons recording-only. Show
the status label for transcribing/cancelling and animate the wave only during
recording. Define widths for idle, recording, and status phases explicitly.

- [ ] **Step 3: Verify and commit**

```bash
bash tests/spec/run_spec.sh
qmllint -I /usr/lib/$(dpkg-architecture -qDEB_HOST_MULTIARCH)/qt6/qml \
  qml/EchoFlowTooltip.qml
git add qml/EchoFlowTooltip.qml tests/spec/run_spec.sh
git commit -m "feat(ui): add idle capsule microphone" -m \
  "Give the pinned capsule a clear mouse entry point while preserving the existing recording, cancel, and transcription presentation."
```

If local DTK QML types are unavailable, record that limitation and rely on the
UI-host build plus focused controller/spec tests.

### Task 5: Wire the Tray and UI Socket

**Files:**
- Modify: `ui-host/main.cpp`
- Modify: `tests/spec/run_spec.sh`

- [ ] **Step 1: Add and run failing wiring checks**

```bash
assert_contains "$ROOT/ui-host/main.cpp" 'QObject::tr("显示录音胶囊")' "tray exposes persistent capsule"
assert_contains "$ROOT/ui-host/main.cpp" 'capsuleAction->setCheckable(true)' "capsule action is checkable"
assert_contains "$ROOT/ui-host/main.cpp" '&CapsuleController::setPersistent' "tray toggles persistent mode"
assert_contains "$ROOT/ui-host/main.cpp" 'controller_->applyServiceMessage(message)' "UI socket forwards session messages"
assert_before "$ROOT/ui-host/main.cpp" 'QObject::tr("显示录音胶囊")' 'QObject::tr("使用引导")' "capsule action precedes guide"
bash tests/spec/run_spec.sh
```

Expected: the new assertions fail.

- [ ] **Step 2: Replace the inline tooltip controller**

Construct the extracted controller with the existing sender and keep the QML
context name stable:

```cpp
CapsuleController controller([controlPath = QString::fromStdString(
                                  defaultControlSocketPath())](std::string_view command) {
    return sendControlCommand(controlPath, command);
});
engine.rootContext()->setContextProperty(QStringLiteral("tooltipController"),
                                         &controller);
```

Remove inline `TooltipController` and position helpers. `UiSocketServer` ignores
legacy `SHOW_TOOLTIP` and forwards every other message to
`controller_->applyServiceMessage(message)`.

- [ ] **Step 3: Add the checkable tray action**

```cpp
QAction *capsuleAction = trayMenu.addAction(QObject::tr("显示录音胶囊"));
capsuleAction->setCheckable(true);
QAction *guideAction = trayMenu.addAction(QObject::tr("使用引导"));
QObject::connect(capsuleAction, &QAction::toggled,
                 &controller, &CapsuleController::setPersistent);
QObject::connect(&controller, &CapsuleController::persistentChanged,
                 capsuleAction, &QAction::setChecked);
```

- [ ] **Step 4: Build, test, and commit**

```bash
cmake --build build --target echoflow-ui test_capsule_controller -j2
ctest --test-dir build -R '^(test_capsule_controller|spec_tests)$' --output-on-failure
git add ui-host/main.cpp tests/spec/run_spec.sh
git commit -m "feat(ui): pin capsule from the tray" -m \
  "Expose the mouse-only voice path through a checkable tray action and route service phases through the tested capsule controller."
```

### Task 6: Preserve Benchmark Meaning

**Files:**
- Modify: `tests/benchmarks/voice_latency_benchmark.cpp`

- [ ] **Step 1: Demonstrate the stale async measurement**

```bash
cmake --build build --target voice_latency_benchmark -j2
./build/tests/voice_latency_benchmark --session-synthetic --iterations 1
```

Expected before the fix: reply is `TRANSCRIBING`, but component timing/UI counts
may print before finalization finishes.

- [ ] **Step 2: Wait for idle with a bounded poll**

```cpp
const auto deadline = Clock::now() + std::chrono::seconds(5);
while (session.state() != echoflow::SessionState::Idle
       && Clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
if (session.state() != echoflow::SessionState::Idle) {
    std::fprintf(stderr, "synthetic voice session did not become idle\n");
    return 1;
}
```

Keep `stop_to_reply_ms` as command-response latency; component fields continue
to report completion costs.

- [ ] **Step 3: Verify and commit**

```bash
cmake --build build --target voice_latency_benchmark -j2
./build/tests/voice_latency_benchmark --session-synthetic --iterations 3
git add tests/benchmarks/voice_latency_benchmark.cpp
git commit -m "test(bench): await voice finalization" -m \
  "Keep synthetic session metrics complete after stop commands become asynchronous."
```

### Task 7: Full and Installed-Runtime Verification

**Files:**
- Modify only if verification exposes a defect in the files above.

- [ ] **Step 1: Run source checks**

```bash
git diff --check
bash -n install-user.sh uninstall-user.sh tests/spec/*.sh
sh -n run.sh
```

- [ ] **Step 2: Run the full build and tests**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j2
ctest --test-dir build --output-on-failure
./build/service/echoflow-service --print-default-config
./build/service/echoflow-service --self-test
```

Expected: build/tests pass; self-test has no new failure. Model availability may
remain an existing host prerequisite.

- [ ] **Step 3: Install and restart the real services**

```bash
./install-user.sh --no-start
systemctl --user daemon-reload
systemctl --user restart echoflow.service echoflow-ui.service
systemctl --user --no-pager --full status echoflow.service echoflow-ui.service
```

- [ ] **Step 4: Verify the interaction matrix**

1. Tray check shows idle capsule and microphone.
2. Microphone starts; check stops; successful commit returns to idle capsule.
3. X during recording discards but leaves persistent idle capsule.
4. Tray uncheck during recording discards and hides.
5. Tray uncheck during transcription hides and commits no text.
6. Immediate recheck during cleanup shows `正在取消`, then idle.
7. With the item unchecked, right Ctrl remains transient and hides on idle.

Run `fcitx5 -rd` before repeating right-Ctrl validation only if the installed
addon/runtime state is stale; this feature does not modify the addon.

- [ ] **Step 5: Inspect final scope**

```bash
git status --short
git log --oneline -8
```

Confirm `docs/performance/intel-npu-gpu-acceleration.md`, model weights,
recordings, build output, and installed artifacts were never staged. Commit any
verification fix only with its matching test and a Conventional Commit body.
