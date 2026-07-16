// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "OnboardingSetupController.h"
#include "OnboardingState.h"
#include "SetupCommandRunner.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class FakeModelSource final : public ModelSetupSource {
    Q_OBJECT
public:
    bool present = false;
    bool running = false;
    int starts = 0;

    bool modelPresent() const override { return present; }
    bool downloadRunning() const override { return running; }
    void startDownload() override
    {
        ++starts;
        running = true;
    }

    void reportProgress(qint64 done, qint64 total) { emit progress(done, total); }
    void finish(bool ok, const QString &error = {})
    {
        running = false;
        present = ok;
        emit finished(ok, error);
    }
};

class FakeCommandRunner final : public SetupCommandRunner {
    Q_OBJECT
public:
    struct Call {
        QString id;
        QString program;
        QStringList arguments;
    };

    QList<Call> calls;

    void run(const QString &id, const QString &program,
             const QStringList &arguments) override
    {
        calls.append({id, program, arguments});
    }

    void finish(const QString &id, bool ok, const QString &error = {})
    {
        emit finished(id, ok, error);
    }
};

class TestOnboardingSetupController : public QObject {
    Q_OBJECT
private slots:
    void allSuccessWritesCompletion();
    void partialFailureDoesNotComplete();
    void retryRunsOnlyFailedItems();
    void duplicateStartIsIgnoredWhileRunning();
    void reconstructsPresentAndRunningModel();
    void completedStateStartsReadyWithoutWork();
    void canceledDownloadIsFailure();
    void persistenceFailureIsAggregateFailure();
    void commandsAndProbesAreExact();
    void reportsModelProgress();
    void processRunnerReportsSuccess();
    void processRunnerReportsNonzeroAndStderr();
    void processRunnerReportsNonzeroAndStdout();
    void processRunnerReportsFailedStart();
    void processRunnerRejectsDuplicateId();
};

static void finishSuccessfulCommands(FakeCommandRunner &runner)
{
    runner.finish(QStringLiteral("ui-autostart"), true);
    runner.finish(QStringLiteral("service"), true);
    runner.finish(QStringLiteral("fcitx"), true);
    runner.finish(QStringLiteral("ui-autostart-check"), true);
    runner.finish(QStringLiteral("service-check"), true);
}

void TestOnboardingSetupController::allSuccessWritesCompletion()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);
    QSignalSpy completeSpy(&controller, &OnboardingSetupController::setupComplete);

    controller.start();
    model.finish(true);
    finishSuccessfulCommands(runner);

    QVERIFY(controller.isComplete());
    QVERIFY(state.isComplete());
    QCOMPARE(completeSpy.count(), 1);
}

void TestOnboardingSetupController::partialFailureDoesNotComplete()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);

    controller.start();
    model.finish(true);
    runner.finish(QStringLiteral("ui-autostart"), true);
    runner.finish(QStringLiteral("service"), true);
    runner.finish(QStringLiteral("fcitx"), false,
                  QStringLiteral("fcitx5 exited with status 1"));
    runner.finish(QStringLiteral("ui-autostart-check"), true);
    runner.finish(QStringLiteral("service-check"), true);

    QVERIFY(!controller.isComplete());
    QVERIFY(!state.isComplete());
    QCOMPARE(controller.itemState(SetupItem::Fcitx), SetupItemState::Failed);
    QCOMPARE(controller.itemError(SetupItem::Fcitx),
             QStringLiteral("fcitx5 exited with status 1"));
}

void TestOnboardingSetupController::retryRunsOnlyFailedItems()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);

    controller.start();
    model.finish(true);
    runner.finish(QStringLiteral("ui-autostart"), true);
    runner.finish(QStringLiteral("service"), true);
    runner.finish(QStringLiteral("fcitx"), false, QStringLiteral("failed"));
    runner.finish(QStringLiteral("ui-autostart-check"), true);
    runner.finish(QStringLiteral("service-check"), true);
    QCOMPARE(runner.calls.size(), 5);

    controller.retryFailed();

    QCOMPARE(runner.calls.size(), 6);
    QCOMPARE(runner.calls.last().id, QStringLiteral("fcitx"));
    QCOMPARE(controller.itemState(SetupItem::Model), SetupItemState::Succeeded);
    QCOMPARE(model.starts, 1);
}

void TestOnboardingSetupController::duplicateStartIsIgnoredWhileRunning()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);

    controller.start();
    controller.start();

    QCOMPARE(model.starts, 1);
    QCOMPARE(runner.calls.size(), 3);
    QVERIFY(controller.isRunning());
    QVERIFY(controller.hasStarted());
}

void TestOnboardingSetupController::reconstructsPresentAndRunningModel()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource presentModel;
    presentModel.present = true;
    FakeCommandRunner firstRunner;
    OnboardingSetupController first(&presentModel, &firstRunner, &state);
    first.start();
    QCOMPARE(first.itemState(SetupItem::Model), SetupItemState::Succeeded);
    QCOMPARE(presentModel.starts, 0);

    FakeModelSource runningModel;
    runningModel.running = true;
    FakeCommandRunner secondRunner;
    OnboardingSetupController second(&runningModel, &secondRunner, &state);
    second.start();
    QCOMPARE(second.itemState(SetupItem::Model), SetupItemState::Running);
    QCOMPARE(runningModel.starts, 0);
}

void TestOnboardingSetupController::completedStateStartsReadyWithoutWork()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    QVERIFY(state.markComplete());
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);

    QVERIFY(controller.isComplete());
    for (SetupItem item : {SetupItem::Model, SetupItem::UiAutostart,
                           SetupItem::Service, SetupItem::Fcitx}) {
        QCOMPARE(controller.itemState(item), SetupItemState::Succeeded);
    }

    controller.start();
    QCOMPARE(model.starts, 0);
    QVERIFY(runner.calls.isEmpty());
}

void TestOnboardingSetupController::canceledDownloadIsFailure()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);

    controller.start();
    model.finish(false, QStringLiteral("Download canceled"));

    QCOMPARE(controller.itemState(SetupItem::Model), SetupItemState::Failed);
    QCOMPARE(controller.itemError(SetupItem::Model),
             QStringLiteral("Download canceled"));
}

void TestOnboardingSetupController::persistenceFailureIsAggregateFailure()
{
    QTemporaryDir dir;
    const QString blockingPath = dir.filePath(QStringLiteral("not-a-directory"));
    QFile file(blockingPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    OnboardingState state(blockingPath + QStringLiteral("/ui-state.ini"));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);
    QSignalSpy failedSpy(&controller, &OnboardingSetupController::setupFailed);
    QSignalSpy completeSpy(&controller, &OnboardingSetupController::setupComplete);

    controller.start();
    model.finish(true);
    finishSuccessfulCommands(runner);

    QVERIFY(!controller.isComplete());
    QVERIFY(!controller.aggregateError().isEmpty());
    QVERIFY(controller.aggregateError().contains(QStringLiteral("onboarding"),
                                                  Qt::CaseInsensitive));
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(completeSpy.count(), 0);
}

void TestOnboardingSetupController::commandsAndProbesAreExact()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);
    controller.start();

    QCOMPARE(runner.calls.at(0).id, QStringLiteral("ui-autostart"));
    QCOMPARE(runner.calls.at(0).program, QStringLiteral("systemctl"));
    QCOMPARE(runner.calls.at(0).arguments,
             QStringList({QStringLiteral("--user"), QStringLiteral("enable"),
                          QStringLiteral("echoflow-ui.service")}));
    QCOMPARE(runner.calls.at(1).id, QStringLiteral("service"));
    QCOMPARE(runner.calls.at(1).arguments,
             QStringList({QStringLiteral("--user"), QStringLiteral("enable"),
                          QStringLiteral("--now"),
                          QStringLiteral("echoflow.service")}));
    QCOMPARE(runner.calls.at(2).id, QStringLiteral("fcitx"));
    QCOMPARE(runner.calls.at(2).program, QStringLiteral("fcitx5"));
    QCOMPARE(runner.calls.at(2).arguments, QStringList({QStringLiteral("-rd")}));

    runner.finish(QStringLiteral("ui-autostart"), true);
    runner.finish(QStringLiteral("service"), true);
    QCOMPARE(controller.itemState(SetupItem::UiAutostart), SetupItemState::Running);
    QCOMPARE(controller.itemState(SetupItem::Service), SetupItemState::Running);
    QCOMPARE(runner.calls.at(3).id, QStringLiteral("ui-autostart-check"));
    QCOMPARE(runner.calls.at(3).program, QStringLiteral("systemctl"));
    QCOMPARE(runner.calls.at(3).arguments,
             QStringList({QStringLiteral("--user"), QStringLiteral("is-enabled"),
                          QStringLiteral("echoflow-ui.service")}));
    QCOMPARE(runner.calls.at(4).id, QStringLiteral("service-check"));
    QCOMPARE(runner.calls.at(4).arguments,
             QStringList({QStringLiteral("--user"), QStringLiteral("is-active"),
                          QStringLiteral("echoflow.service")}));

    runner.finish(QStringLiteral("ui-autostart-check"), false,
                  QStringLiteral("disabled"));
    runner.finish(QStringLiteral("service-check"), true);
    QCOMPARE(controller.itemState(SetupItem::UiAutostart), SetupItemState::Failed);
    QCOMPARE(controller.itemError(SetupItem::UiAutostart), QStringLiteral("disabled"));
    QCOMPARE(controller.itemState(SetupItem::Service), SetupItemState::Succeeded);
}

void TestOnboardingSetupController::reportsModelProgress()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);
    QSignalSpy progressSpy(&controller, &OnboardingSetupController::progressChanged);
    controller.start();

    model.reportProgress(12, 40);

    QCOMPARE(controller.progressDone(), 12);
    QCOMPARE(controller.progressTotal(), 40);
    QCOMPARE(progressSpy.count(), 1);
}

void TestOnboardingSetupController::processRunnerReportsSuccess()
{
    QProcessSetupCommandRunner runner;
    QSignalSpy spy(&runner, &SetupCommandRunner::finished);
    runner.run(QStringLiteral("success"), QStringLiteral("/bin/sh"),
               {QStringLiteral("-c"), QStringLiteral("exit 0")});
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 2000);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("success"));
    QCOMPARE(spy.first().at(1).toBool(), true);
}

void TestOnboardingSetupController::processRunnerReportsNonzeroAndStderr()
{
    QProcessSetupCommandRunner runner;
    QSignalSpy spy(&runner, &SetupCommandRunner::finished);
    runner.run(QStringLiteral("failure"), QStringLiteral("/bin/sh"),
               {QStringLiteral("-c"), QStringLiteral("echo broken >&2; exit 7")});
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 2000);
    QCOMPARE(spy.first().at(1).toBool(), false);
    QVERIFY(spy.first().at(2).toString().contains(QStringLiteral("broken")));
}

void TestOnboardingSetupController::processRunnerReportsNonzeroAndStdout()
{
    QProcessSetupCommandRunner runner;
    QSignalSpy spy(&runner, &SetupCommandRunner::finished);
    runner.run(QStringLiteral("inactive"), QStringLiteral("/bin/sh"),
               {QStringLiteral("-c"), QStringLiteral("echo inactive; exit 3")});
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 2000);
    QCOMPARE(spy.first().at(1).toBool(), false);
    QVERIFY(spy.first().at(2).toString().contains(QStringLiteral("inactive")));
}

void TestOnboardingSetupController::processRunnerReportsFailedStart()
{
    QProcessSetupCommandRunner runner;
    QSignalSpy spy(&runner, &SetupCommandRunner::finished);
    runner.run(QStringLiteral("missing"),
               QStringLiteral("/definitely/not/an/echoflow-command"), {});
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 2000);
    QCOMPARE(spy.first().at(1).toBool(), false);
    QVERIFY(!spy.first().at(2).toString().isEmpty());
}

void TestOnboardingSetupController::processRunnerRejectsDuplicateId()
{
    QProcessSetupCommandRunner runner;
    QSignalSpy spy(&runner, &SetupCommandRunner::finished);
    runner.run(QStringLiteral("same"), QStringLiteral("/bin/sh"),
               {QStringLiteral("-c"), QStringLiteral("sleep 0.1")});
    runner.run(QStringLiteral("same"), QStringLiteral("/bin/sh"),
               {QStringLiteral("-c"), QStringLiteral("exit 0")});

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 2000);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("same"));
    QCOMPARE(spy.at(0).at(1).toBool(), false);
    QVERIFY(spy.at(0).at(2).toString().contains(QStringLiteral("already running")));
    QCOMPARE(spy.at(1).at(1).toBool(), true);
}

QTEST_GUILESS_MAIN(TestOnboardingSetupController)

#include "test_onboarding_setup_controller.moc"
