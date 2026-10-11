// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QGuiApplication>
#include <QScreen>
#include <QSignalSpy>
#include <QtTest>

#include "CapsuleController.h"

#include <string>
#include <string_view>
#include <vector>

namespace {

struct CapsuleState {
    bool visible = false;
    QString phase;
    QString message;
    bool hasPosition = false;
    int moveX = 0;
    int moveY = 0;
    bool anchorTopLeft = false;
};

struct HintState {
    bool visible = false;
    QString text;
    int moveX = 0;
    int moveY = 0;
};

HintState hintAt(const QSignalSpy &spy, int index) {
    const QList<QVariant> arguments = spy.at(index);
    return {
        arguments.at(0).toBool(),
        arguments.at(1).toString(),
        arguments.at(2).toInt(),
        arguments.at(3).toInt(),
    };
}

// Hint updates are coalesced, so let them land before inspecting the spy.
void settleHint() {
    QTest::qWait(CapsuleController::kHintSettleMs * 3);
}

CapsuleController::SendCommand ignoreCommands() {
    return [](std::string_view) { return true; };
}

CapsuleState stateAt(const QSignalSpy &spy, int index) {
    const QList<QVariant> arguments = spy.at(index);
    return {
        arguments.at(0).toBool(),
        arguments.at(1).toString(),
        arguments.at(2).toString(),
        arguments.at(3).toBool(),
        arguments.at(4).toInt(),
        arguments.at(5).toInt(),
        arguments.at(6).toBool(),
    };
}

} // namespace

class CapsuleControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void persistentIdleIsVisibleAndRequestsStatus();
    void transientIdleIsHidden();
    void closingPersistentCapsuleHidesAndCancels();
    void reopeningBeforeIdleShowsCancelling();
    void persistentIdleMessageRemainsVisible();
    void transientRecordingAndTranscribingAreVisibleUntilIdle();
    void cancellingIsVisibleOnlyInPersistentMode();
    void streamTextOnlyUpdatesRecording();
    void toggleAndCancelSendCommands();
    void persistentFlagOnlySignalsActualChanges();
    void focusShowsHintBelowCaret();
    void hintWaitsForCaretAfterFocus();
    void hintFollowsCaretUntilTimeout();
    void typingOrBlurHidesHintForGood();
    void hintTextReflectsLongPress();
    void hintSuppressedWhenDisabledPersistentOrBusy();
    void recordingAnchorsCapsuleBesideCaret();
    void capsuleWithoutCaretStaysAtBottom();
    void persistentCapsuleIgnoresCaret();
    void inputFocusIsReportedForLongPress();
    void clickFocusAnchorsAtPress();
    void pressArrivingAfterFocusReanchors();
    void staleCaretEchoKeepsPressAnchor();
    void focusChurnDoesNotFlickerHint();
};

void CapsuleControllerTest::persistentIdleIsVisibleAndRequestsStatus() {
    std::vector<std::string> commands;
    CapsuleController controller(
        [&](std::string_view command) {
            commands.emplace_back(command);
            return true;
        });
    QSignalSpy capsuleSpy(&controller, &CapsuleController::capsuleChanged);

    controller.setPersistent(true);

    QCOMPARE(commands, std::vector<std::string>{"STATUS"});
    QCOMPARE(capsuleSpy.count(), 1);
    const CapsuleState state = stateAt(capsuleSpy, 0);
    QVERIFY(state.visible);
    QCOMPARE(state.phase, QStringLiteral("idle"));
    QVERIFY(state.message.isEmpty());
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect available = screen->availableGeometry();
        QVERIFY(state.hasPosition);
        QCOMPARE(state.moveX, available.left() + available.width() / 2);
        QCOMPARE(state.moveY, available.bottom() - 8);
    } else {
        QVERIFY(!state.hasPosition);
    }
}

void CapsuleControllerTest::transientIdleIsHidden() {
    CapsuleController controller([](std::string_view) { return true; });
    QSignalSpy capsuleSpy(&controller, &CapsuleController::capsuleChanged);

    controller.applyServiceMessage(QStringLiteral("IDLE"));

    QCOMPARE(capsuleSpy.count(), 1);
    const CapsuleState state = stateAt(capsuleSpy, 0);
    QVERIFY(!state.visible);
    QCOMPARE(state.phase, QStringLiteral("idle"));
    QVERIFY(state.message.isEmpty());
}

void CapsuleControllerTest::closingPersistentCapsuleHidesAndCancels() {
    std::vector<std::string> commands;
    CapsuleController controller(
        [&](std::string_view command) {
            commands.emplace_back(command);
            return true;
        });
    controller.setPersistent(true);
    controller.applyServiceMessage(QStringLiteral("RECORDING"));
    QSignalSpy capsuleSpy(&controller, &CapsuleController::capsuleChanged);

    controller.setPersistent(false);

    QCOMPARE(commands, (std::vector<std::string>{"STATUS", "CANCEL"}));
    QCOMPARE(capsuleSpy.count(), 1);
    const CapsuleState state = stateAt(capsuleSpy, 0);
    QVERIFY(!state.visible);
    QCOMPARE(state.phase, QStringLiteral("cancelling"));
    QCOMPARE(state.message, QStringLiteral("正在取消"));
}

void CapsuleControllerTest::reopeningBeforeIdleShowsCancelling() {
    CapsuleController controller([](std::string_view) { return true; });
    controller.setPersistent(true);
    controller.applyServiceMessage(QStringLiteral("RECORDING"));
    controller.setPersistent(false);
    QSignalSpy capsuleSpy(&controller, &CapsuleController::capsuleChanged);

    controller.setPersistent(true);

    QCOMPARE(capsuleSpy.count(), 1);
    const CapsuleState state = stateAt(capsuleSpy, 0);
    QVERIFY(state.visible);
    QCOMPARE(state.phase, QStringLiteral("cancelling"));
    QCOMPARE(state.message, QStringLiteral("正在取消"));
}

void CapsuleControllerTest::persistentIdleMessageRemainsVisible() {
    CapsuleController controller([](std::string_view) { return true; });
    controller.setPersistent(true);
    controller.applyServiceMessage(QStringLiteral("RECORDING"));
    QSignalSpy capsuleSpy(&controller, &CapsuleController::capsuleChanged);

    controller.applyServiceMessage(QStringLiteral("IDLE"));

    QCOMPARE(capsuleSpy.count(), 1);
    const CapsuleState state = stateAt(capsuleSpy, 0);
    QVERIFY(state.visible);
    QCOMPARE(state.phase, QStringLiteral("idle"));
    QVERIFY(state.message.isEmpty());
}

void CapsuleControllerTest::transientRecordingAndTranscribingAreVisibleUntilIdle() {
    CapsuleController controller([](std::string_view) { return true; });
    QSignalSpy capsuleSpy(&controller, &CapsuleController::capsuleChanged);

    controller.applyServiceMessage(QStringLiteral("RECORDING"));
    controller.applyServiceMessage(QStringLiteral("TRANSCRIBING"));
    controller.applyServiceMessage(QStringLiteral("IDLE"));

    QCOMPARE(capsuleSpy.count(), 3);
    CapsuleState state = stateAt(capsuleSpy, 0);
    QVERIFY(state.visible);
    QCOMPARE(state.phase, QStringLiteral("recording"));
    QCOMPARE(state.message, QStringLiteral("正在聆听"));
    state = stateAt(capsuleSpy, 1);
    QVERIFY(state.visible);
    QCOMPARE(state.phase, QStringLiteral("transcribing"));
    QCOMPARE(state.message, QStringLiteral("正在转写"));
    state = stateAt(capsuleSpy, 2);
    QVERIFY(!state.visible);
    QCOMPARE(state.phase, QStringLiteral("idle"));
}

void CapsuleControllerTest::cancellingIsVisibleOnlyInPersistentMode() {
    CapsuleController controller([](std::string_view) { return true; });
    QSignalSpy capsuleSpy(&controller, &CapsuleController::capsuleChanged);

    controller.applyServiceMessage(QStringLiteral("CANCELLING"));
    controller.setPersistent(true);
    controller.applyServiceMessage(QStringLiteral("CANCELLING"));

    QCOMPARE(capsuleSpy.count(), 3);
    QVERIFY(!stateAt(capsuleSpy, 0).visible);
    QVERIFY(stateAt(capsuleSpy, 1).visible);
    const CapsuleState persistentState = stateAt(capsuleSpy, 2);
    QVERIFY(persistentState.visible);
    QCOMPARE(persistentState.phase, QStringLiteral("cancelling"));
    QCOMPARE(persistentState.message, QStringLiteral("正在取消"));
}

void CapsuleControllerTest::streamTextOnlyUpdatesRecording() {
    CapsuleController controller([](std::string_view) { return true; });
    QSignalSpy capsuleSpy(&controller, &CapsuleController::capsuleChanged);

    QVERIFY(!controller.applyServiceMessage(QStringLiteral("STREAM_TEXT ignored")));
    QVERIFY(controller.applyServiceMessage(QStringLiteral("RECORDING")));
    QVERIFY(!controller.applyServiceMessage(QStringLiteral("STREAM_TEXT ")));
    QVERIFY(controller.applyServiceMessage(QStringLiteral("STREAM_TEXT 实时文字")));
    QVERIFY(controller.applyServiceMessage(QStringLiteral("TRANSCRIBING")));
    QVERIFY(!controller.applyServiceMessage(QStringLiteral("STREAM_TEXT late")));

    QCOMPARE(capsuleSpy.count(), 3);
    QCOMPARE(stateAt(capsuleSpy, 0).message, QStringLiteral("正在聆听"));
    QCOMPARE(stateAt(capsuleSpy, 1).message, QStringLiteral("实时文字"));
    QCOMPARE(stateAt(capsuleSpy, 2).message, QStringLiteral("正在转写"));
}

void CapsuleControllerTest::toggleAndCancelSendCommands() {
    std::vector<std::string> commands;
    CapsuleController controller(
        [&](std::string_view command) {
            commands.emplace_back(command);
            return true;
        });

    controller.requestToggle();
    controller.requestCancel();

    QCOMPARE(commands, (std::vector<std::string>{"CTRL_DOWN", "CANCEL"}));
    QVERIFY(!controller.persistent());
}

void CapsuleControllerTest::persistentFlagOnlySignalsActualChanges() {
    std::vector<std::string> commands;
    CapsuleController controller(
        [&](std::string_view command) {
            commands.emplace_back(command);
            return true;
        });
    QSignalSpy persistentSpy(&controller, &CapsuleController::persistentChanged);

    QVERIFY(!controller.persistent());
    controller.setPersistent(false);
    controller.setPersistent(true);
    controller.setPersistent(true);
    controller.setPersistent(false);
    controller.setPersistent(false);

    QVERIFY(!controller.persistent());
    QCOMPARE(persistentSpy.count(), 2);
    QCOMPARE(persistentSpy.at(0).at(0).toBool(), true);
    QCOMPARE(persistentSpy.at(1).at(0).toBool(), false);
    QCOMPARE(commands, (std::vector<std::string>{"STATUS", "CANCEL"}));
}

void CapsuleControllerTest::focusShowsHintBelowCaret() {
    CapsuleController controller(ignoreCommands());
    QSignalSpy hintSpy(&controller, &CapsuleController::hintChanged);

    QVERIFY(controller.applyServiceMessage(QStringLiteral("FOCUS 100 200 2 18")));

    settleHint();

    QCOMPARE(hintSpy.count(), 1);
    const HintState hint = hintAt(hintSpy, 0);
    QVERIFY(hint.visible);
    QCOMPARE(hint.moveX, 100);
    QCOMPARE(hint.moveY, 200 + 18 + CapsuleController::kCaretGap);
}

void CapsuleControllerTest::hintWaitsForCaretAfterFocus() {
    CapsuleController controller(ignoreCommands());
    QSignalSpy hintSpy(&controller, &CapsuleController::hintChanged);

    controller.applyServiceMessage(QStringLiteral("FOCUS"));

    settleHint();
    QVERIFY(hintSpy.isEmpty() || !hintAt(hintSpy, hintSpy.count() - 1).visible);

    controller.applyServiceMessage(QStringLiteral("CARET 40 60 0 20"));

    settleHint();
    const HintState hint = hintAt(hintSpy, hintSpy.count() - 1);
    QVERIFY(hint.visible);
    QCOMPARE(hint.moveX, 40);
    QCOMPARE(hint.moveY, 60 + 20 + CapsuleController::kCaretGap);

    // A caret without a preceding focus-in never shows a hint.
    CapsuleController unfocused(ignoreCommands());
    QSignalSpy unfocusedSpy(&unfocused, &CapsuleController::hintChanged);
    unfocused.applyServiceMessage(QStringLiteral("CARET 40 60 0 20"));
    settleHint();
    QVERIFY(unfocusedSpy.isEmpty());
}

void CapsuleControllerTest::hintFollowsCaretUntilTimeout() {
    CapsuleController controller(ignoreCommands());
    controller.setHintTimeout(300);
    QSignalSpy hintSpy(&controller, &CapsuleController::hintChanged);

    controller.applyServiceMessage(QStringLiteral("FOCUS 100 200 2 18"));

    settleHint();
    controller.applyServiceMessage(QStringLiteral("CARET 150 200 2 18"));
    settleHint();
    QCOMPARE(hintSpy.count(), 2);
    QVERIFY(hintAt(hintSpy, 1).visible);
    QCOMPARE(hintAt(hintSpy, 1).moveX, 150);

    QTRY_VERIFY(!hintAt(hintSpy, hintSpy.count() - 1).visible);

    // After timing out, caret moves in the same field stay quiet.
    const int count = hintSpy.count();
    controller.applyServiceMessage(QStringLiteral("CARET 180 200 2 18"));
    settleHint();
    QCOMPARE(hintSpy.count(), count);

    // A new focus-in gets a new hint.
    controller.applyServiceMessage(QStringLiteral("FOCUS 10 20 2 18"));
    settleHint();
    QVERIFY(hintAt(hintSpy, hintSpy.count() - 1).visible);
}

void CapsuleControllerTest::typingOrBlurHidesHintForGood() {
    CapsuleController controller(ignoreCommands());
    QSignalSpy hintSpy(&controller, &CapsuleController::hintChanged);

    controller.applyServiceMessage(QStringLiteral("FOCUS 100 200 2 18"));

    settleHint();
    QVERIFY(controller.applyServiceMessage(QStringLiteral("TYPED")));
    settleHint();
    QVERIFY(!hintAt(hintSpy, hintSpy.count() - 1).visible);
    controller.applyServiceMessage(QStringLiteral("CARET 110 200 2 18"));
    settleHint();
    QVERIFY(!hintAt(hintSpy, hintSpy.count() - 1).visible);

    controller.applyServiceMessage(QStringLiteral("FOCUS 100 200 2 18"));

    settleHint();
    QVERIFY(hintAt(hintSpy, hintSpy.count() - 1).visible);
    QVERIFY(controller.applyServiceMessage(QStringLiteral("BLUR")));
    settleHint();
    QVERIFY(!hintAt(hintSpy, hintSpy.count() - 1).visible);
}

void CapsuleControllerTest::hintTextReflectsLongPress() {
    CapsuleController controller(ignoreCommands());
    QSignalSpy hintSpy(&controller, &CapsuleController::hintChanged);

    controller.applyServiceMessage(QStringLiteral("FOCUS 100 200 2 18"));

    settleHint();
    QCOMPARE(hintAt(hintSpy, hintSpy.count() - 1).text, QStringLiteral("按右 Ctrl 说话"));

    controller.setLongPressHint(true);

    settleHint();
    QCOMPARE(hintAt(hintSpy, hintSpy.count() - 1).text,
             QStringLiteral("长按鼠标或按右 Ctrl 说话"));
}

void CapsuleControllerTest::hintSuppressedWhenDisabledPersistentOrBusy() {
    CapsuleController disabled(ignoreCommands());
    disabled.setFocusHintEnabled(false);
    QSignalSpy disabledSpy(&disabled, &CapsuleController::hintChanged);
    disabled.applyServiceMessage(QStringLiteral("FOCUS 100 200 2 18"));
    settleHint();
    QVERIFY(disabledSpy.isEmpty() || !hintAt(disabledSpy, disabledSpy.count() - 1).visible);

    CapsuleController persistent(ignoreCommands());
    persistent.setPersistent(true);
    QSignalSpy persistentSpy(&persistent, &CapsuleController::hintChanged);
    persistent.applyServiceMessage(QStringLiteral("FOCUS 100 200 2 18"));
    settleHint();
    QVERIFY(persistentSpy.isEmpty()
            || !hintAt(persistentSpy, persistentSpy.count() - 1).visible);

    CapsuleController busy(ignoreCommands());
    QSignalSpy busySpy(&busy, &CapsuleController::hintChanged);
    busy.applyServiceMessage(QStringLiteral("FOCUS 100 200 2 18"));
    settleHint();
    QVERIFY(hintAt(busySpy, busySpy.count() - 1).visible);
    busy.applyServiceMessage(QStringLiteral("RECORDING"));
    settleHint();
    QVERIFY(!hintAt(busySpy, busySpy.count() - 1).visible);
    // The hint does not come back once the session finishes.
    busy.applyServiceMessage(QStringLiteral("IDLE"));
    settleHint();
    QVERIFY(!hintAt(busySpy, busySpy.count() - 1).visible);
}

void CapsuleControllerTest::recordingAnchorsCapsuleBesideCaret() {
    CapsuleController controller(ignoreCommands());
    QSignalSpy capsuleSpy(&controller, &CapsuleController::capsuleChanged);

    controller.applyServiceMessage(QStringLiteral("FOCUS 100 200 2 18"));
    controller.applyServiceMessage(QStringLiteral("RECORDING"));
    // Caret updates during recording do not move the capsule.
    controller.applyServiceMessage(QStringLiteral("CARET 300 400 2 18"));
    controller.applyServiceMessage(QStringLiteral("TRANSCRIBING"));

    QCOMPARE(capsuleSpy.count(), 2);
    for (int i = 0; i < capsuleSpy.count(); ++i) {
        const CapsuleState state = stateAt(capsuleSpy, i);
        QVERIFY(state.visible);
        QVERIFY(state.hasPosition);
        QVERIFY(state.anchorTopLeft);
        QCOMPARE(state.moveX, 100);
        QCOMPARE(state.moveY, 200 + 18 + CapsuleController::kCaretGap);
    }
}

void CapsuleControllerTest::capsuleWithoutCaretStaysAtBottom() {
    CapsuleController controller(ignoreCommands());
    QSignalSpy capsuleSpy(&controller, &CapsuleController::capsuleChanged);

    controller.applyServiceMessage(QStringLiteral("FOCUS"));
    controller.applyServiceMessage(QStringLiteral("RECORDING"));

    QCOMPARE(capsuleSpy.count(), 1);
    QVERIFY(!stateAt(capsuleSpy, 0).anchorTopLeft);

    // The anchor is cleared at idle, so a later session without focus is
    // not placed beside a stale caret.
    controller.applyServiceMessage(QStringLiteral("CARET 100 200 2 18"));
    controller.applyServiceMessage(QStringLiteral("IDLE"));
    controller.applyServiceMessage(QStringLiteral("BLUR"));
    controller.applyServiceMessage(QStringLiteral("RECORDING"));
    QVERIFY(!stateAt(capsuleSpy, capsuleSpy.count() - 1).anchorTopLeft);
}

void CapsuleControllerTest::persistentCapsuleIgnoresCaret() {
    CapsuleController controller(ignoreCommands());
    controller.setPersistent(true);
    QSignalSpy capsuleSpy(&controller, &CapsuleController::capsuleChanged);

    controller.applyServiceMessage(QStringLiteral("FOCUS 100 200 2 18"));
    controller.applyServiceMessage(QStringLiteral("RECORDING"));

    QCOMPARE(capsuleSpy.count(), 1);
    QVERIFY(!stateAt(capsuleSpy, 0).anchorTopLeft);
}

void CapsuleControllerTest::inputFocusIsReportedForLongPress() {
    CapsuleController controller(ignoreCommands());
    QSignalSpy focusSpy(&controller, &CapsuleController::inputFocusChanged);

    controller.applyServiceMessage(QStringLiteral("FOCUS 100 200 2 18"));
    controller.applyServiceMessage(QStringLiteral("CARET 120 200 2 18"));
    controller.applyServiceMessage(QStringLiteral("TYPED"));
    controller.applyServiceMessage(QStringLiteral("CARET 1 2 bad 4"));
    controller.applyServiceMessage(QStringLiteral("BLUR"));

    QCOMPARE(focusSpy.count(), 4);
    QVERIFY(focusSpy.at(0).at(0).toBool());
    QCOMPARE(focusSpy.at(0).at(1).toRect(), QRect(100, 200, 2, 18));
    QCOMPARE(focusSpy.at(1).at(1).toRect(), QRect(120, 200, 2, 18));
    QVERIFY(focusSpy.at(2).at(0).toBool());
    QVERIFY(!focusSpy.at(2).at(1).toRect().isValid());
    QVERIFY(!focusSpy.at(3).at(0).toBool());
}

void CapsuleControllerTest::clickFocusAnchorsAtPress() {
    // Chromium reports the previous field's caret when focus moves between
    // its inputs; the click that focused the new field is the better anchor.
    CapsuleController controller(ignoreCommands());
    QSignalSpy hintSpy(&controller, &CapsuleController::hintChanged);
    QSignalSpy focusSpy(&controller, &CapsuleController::inputFocusChanged);

    controller.notePointerPress(QPoint(700, 500));
    controller.applyServiceMessage(QStringLiteral("FOCUS 100 200 2 18"));
    settleHint();

    const QRect anchor(700, 491, 0, 18);
    QCOMPARE(focusSpy.at(focusSpy.count() - 1).at(1).toRect(), anchor);
    const HintState hint = hintAt(hintSpy, hintSpy.count() - 1);
    QVERIFY(hint.visible);
    QCOMPARE(hint.moveX, 700);
    QCOMPARE(hint.moveY, anchor.bottom() + 1 + CapsuleController::kCaretGap);

    // Keyboard focus long after the press keeps using the reported caret.
    QTest::qWait(CapsuleController::kClickFocusWindowMs + 50);
    controller.applyServiceMessage(QStringLiteral("BLUR"));
    controller.applyServiceMessage(QStringLiteral("FOCUS 100 200 2 18"));
    QCOMPARE(focusSpy.at(focusSpy.count() - 1).at(1).toRect(), QRect(100, 200, 2, 18));
}

void CapsuleControllerTest::pressArrivingAfterFocusReanchors() {
    CapsuleController controller(ignoreCommands());
    QSignalSpy focusSpy(&controller, &CapsuleController::inputFocusChanged);

    controller.applyServiceMessage(QStringLiteral("FOCUS 100 200 2 18"));
    controller.notePointerPress(QPoint(700, 500));

    QCOMPARE(focusSpy.count(), 2);
    QCOMPARE(focusSpy.at(1).at(1).toRect(), QRect(700, 491, 0, 18));
}

void CapsuleControllerTest::staleCaretEchoKeepsPressAnchor() {
    CapsuleController controller(ignoreCommands());
    QSignalSpy focusSpy(&controller, &CapsuleController::inputFocusChanged);

    controller.notePointerPress(QPoint(700, 500));
    controller.applyServiceMessage(QStringLiteral("FOCUS 100 200 2 18"));
    const int count = focusSpy.count();

    // The stale value echoed again is ignored...
    controller.applyServiceMessage(QStringLiteral("CARET 100 200 2 18"));
    QCOMPARE(focusSpy.count(), count);

    // ...while a genuinely new caret replaces the press anchor.
    controller.applyServiceMessage(QStringLiteral("CARET 720 495 2 18"));
    QCOMPARE(focusSpy.at(focusSpy.count() - 1).at(1).toRect(), QRect(720, 495, 2, 18));
}

void CapsuleControllerTest::focusChurnDoesNotFlickerHint() {
    CapsuleController controller(ignoreCommands());
    QSignalSpy hintSpy(&controller, &CapsuleController::hintChanged);

    controller.notePointerPress(QPoint(700, 500));
    controller.applyServiceMessage(QStringLiteral("BLUR"));
    controller.applyServiceMessage(QStringLiteral("FOCUS 100 200 2 18"));
    controller.applyServiceMessage(QStringLiteral("BLUR"));
    controller.applyServiceMessage(QStringLiteral("FOCUS 100 200 2 18"));
    settleHint();

    QCOMPARE(hintSpy.count(), 1);
    QVERIFY(hintAt(hintSpy, 0).visible);
}

QTEST_MAIN(CapsuleControllerTest)

#include "test_capsule_controller.moc"
