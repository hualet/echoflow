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
};

CapsuleState stateAt(const QSignalSpy &spy, int index) {
    const QList<QVariant> arguments = spy.at(index);
    return {
        arguments.at(0).toBool(),
        arguments.at(1).toString(),
        arguments.at(2).toString(),
        arguments.at(3).toBool(),
        arguments.at(4).toInt(),
        arguments.at(5).toInt(),
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

QTEST_MAIN(CapsuleControllerTest)

#include "test_capsule_controller.moc"
