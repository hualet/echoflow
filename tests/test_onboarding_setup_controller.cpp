// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "OnboardingSetupController.h"
#include "OnboardingState.h"
#include "ModelDownloadCoordinator.h"
#include "ModelSetupAdapter.h"
#include "SetupCommandRunner.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <stdexcept>

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
    void reconstructionPreservesReadyItemsWithoutMutation();
    void reconstructionLeavesNotReadyItemsPending();
    void startRunsOnlyMissingReconstructedItems();
    void startPreservesRunningModelAndLaunchesOtherItems();
    void startDuringReconstructionIsQueued();
    void reconstructionTimeoutIsRecoverable();
    void canceledDownloadIsFailure();
    void persistenceFailureIsAggregateFailure();
    void commandsAndProbesAreExact();
    void reportsModelProgress();
    void processRunnerReportsSuccess();
    void processRunnerReportsNonzeroAndStderr();
    void processRunnerReportsNonzeroAndStdout();
    void processRunnerReportsFailedStart();
    void processRunnerRejectsDuplicateId();
    void processRunnerTimesOutAndReusesId();
    void modelAdapterDetectsPresentModel();
    void modelAdapterDetectsMissingModel();
    void modelAdapterMapsRetainedDownloadingSnapshot();
    void modelAdapterStartsDefaultModel_data();
    void modelAdapterStartsDefaultModel();
    void modelAdapterRefreshesMirrorBeforeEachStart();
    void modelAdapterRejectsEmptyDependencies();
    void modelAdapterFiltersCoordinatorSignals();
    void modelAdapterObservationIsIdempotentAndSwitchable();
};

static void finishSuccessfulCommands(FakeCommandRunner &runner)
{
    runner.finish(QStringLiteral("ui-autostart"), true);
    runner.finish(QStringLiteral("service"), true);
    runner.finish(QStringLiteral("fcitx"), true);
    runner.finish(QStringLiteral("ui-autostart-check"), true);
    runner.finish(QStringLiteral("service-check"), true);
}

static void finishInitialNotReady(FakeCommandRunner &runner)
{
    runner.finish(QStringLiteral("ui-autostart-initial-check"), false,
                  QStringLiteral("disabled"));
    runner.finish(QStringLiteral("service-initial-check"), false,
                  QStringLiteral("inactive"));
    runner.calls.clear();
}

void TestOnboardingSetupController::allSuccessWritesCompletion()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);
    QSignalSpy completeSpy(&controller, &OnboardingSetupController::setupComplete);

    finishInitialNotReady(runner);
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

    finishInitialNotReady(runner);
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

    finishInitialNotReady(runner);
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

    finishInitialNotReady(runner);
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
    QCOMPARE(first.itemState(SetupItem::Model), SetupItemState::Succeeded);
    QCOMPARE(presentModel.starts, 0);
    QVERIFY(!first.hasStarted());

    FakeModelSource runningModel;
    runningModel.running = true;
    FakeCommandRunner secondRunner;
    OnboardingSetupController second(&runningModel, &secondRunner, &state);
    QCOMPARE(second.itemState(SetupItem::Model), SetupItemState::Running);
    QCOMPARE(runningModel.starts, 0);
    QVERIFY(!second.hasStarted());
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
    QVERIFY(runner.calls.isEmpty());
    for (SetupItem item : {SetupItem::Model, SetupItem::UiAutostart,
                           SetupItem::Service, SetupItem::Fcitx}) {
        QCOMPARE(controller.itemState(item), SetupItemState::Succeeded);
    }

    controller.start();
    QCOMPARE(model.starts, 0);
    QVERIFY(runner.calls.isEmpty());
}

void TestOnboardingSetupController::reconstructionPreservesReadyItemsWithoutMutation()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);

    QCOMPARE(runner.calls.size(), 2);
    QCOMPARE(runner.calls.at(0).id, QStringLiteral("ui-autostart-initial-check"));
    QCOMPARE(runner.calls.at(0).program, QStringLiteral("systemctl"));
    QCOMPARE(runner.calls.at(0).arguments,
             QStringList({QStringLiteral("--user"), QStringLiteral("is-enabled"),
                          QStringLiteral("echoflow-ui.service")}));
    QCOMPARE(runner.calls.at(1).id, QStringLiteral("service-initial-check"));
    QCOMPARE(runner.calls.at(1).program, QStringLiteral("systemctl"));
    QCOMPARE(runner.calls.at(1).arguments,
             QStringList({QStringLiteral("--user"), QStringLiteral("is-active"),
                          QStringLiteral("echoflow.service")}));
    QVERIFY(!controller.hasStarted());
    runner.finish(QStringLiteral("ui-autostart-initial-check"), true);
    runner.finish(QStringLiteral("service-initial-check"), true);
    QCOMPARE(controller.itemState(SetupItem::UiAutostart),
             SetupItemState::Succeeded);
    QCOMPARE(controller.itemState(SetupItem::Service), SetupItemState::Succeeded);

    runner.calls.clear();
    controller.start();

    QCOMPARE(model.starts, 1);
    QCOMPARE(runner.calls.size(), 1);
    QCOMPARE(runner.calls.first().id, QStringLiteral("fcitx"));
}

void TestOnboardingSetupController::reconstructionLeavesNotReadyItemsPending()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);

    runner.finish(QStringLiteral("ui-autostart-initial-check"), false,
                  QStringLiteral("disabled"));
    runner.finish(QStringLiteral("service-initial-check"), false,
                  QStringLiteral("inactive"));

    QCOMPARE(controller.itemState(SetupItem::UiAutostart), SetupItemState::Pending);
    QCOMPARE(controller.itemState(SetupItem::Service), SetupItemState::Pending);
    QVERIFY(controller.itemError(SetupItem::UiAutostart).isEmpty());
    QVERIFY(controller.itemError(SetupItem::Service).isEmpty());
    QVERIFY(controller.aggregateError().isEmpty());
    QVERIFY(!controller.hasStarted());
}

void TestOnboardingSetupController::startRunsOnlyMissingReconstructedItems()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    model.present = true;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);
    runner.finish(QStringLiteral("ui-autostart-initial-check"), true);
    runner.finish(QStringLiteral("service-initial-check"), false,
                  QStringLiteral("inactive"));
    runner.calls.clear();

    controller.start();

    QCOMPARE(model.starts, 0);
    QCOMPARE(runner.calls.size(), 2);
    QCOMPARE(runner.calls.at(0).id, QStringLiteral("service"));
    QCOMPARE(runner.calls.at(1).id, QStringLiteral("fcitx"));
}

void TestOnboardingSetupController::startPreservesRunningModelAndLaunchesOtherItems()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    model.running = true;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);
    finishInitialNotReady(runner);

    controller.start();

    QCOMPARE(model.starts, 0);
    QCOMPARE(controller.itemState(SetupItem::Model), SetupItemState::Running);
    QCOMPARE(runner.calls.size(), 3);
    QCOMPARE(runner.calls.at(0).id, QStringLiteral("ui-autostart"));
    QCOMPARE(runner.calls.at(1).id, QStringLiteral("service"));
    QCOMPARE(runner.calls.at(2).id, QStringLiteral("fcitx"));
}

void TestOnboardingSetupController::startDuringReconstructionIsQueued()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);

    controller.start();
    QVERIFY(controller.hasStarted());
    QCOMPARE(runner.calls.size(), 2);
    QCOMPARE(model.starts, 0);
    runner.finish(QStringLiteral("ui-autostart-initial-check"), true);
    QCOMPARE(runner.calls.size(), 2);
    runner.finish(QStringLiteral("service-initial-check"), false,
                  QStringLiteral("inactive"));

    QCOMPARE(model.starts, 1);
    QCOMPARE(runner.calls.size(), 4);
    QCOMPARE(runner.calls.at(2).id, QStringLiteral("service"));
    QCOMPARE(runner.calls.at(3).id, QStringLiteral("fcitx"));
}

void TestOnboardingSetupController::reconstructionTimeoutIsRecoverable()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    model.present = true;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);
    runner.finish(QStringLiteral("ui-autostart-initial-check"), false,
                  QStringLiteral("systemctl timed out after 50 ms"));
    runner.finish(QStringLiteral("service-initial-check"), false,
                  QStringLiteral("systemctl failed to start"));

    QCOMPARE(controller.itemState(SetupItem::UiAutostart), SetupItemState::Pending);
    QCOMPARE(controller.itemState(SetupItem::Service), SetupItemState::Pending);
    QVERIFY(controller.itemError(SetupItem::UiAutostart).isEmpty());
    QVERIFY(controller.itemError(SetupItem::Service).isEmpty());
    runner.calls.clear();

    controller.start();

    QCOMPARE(runner.calls.size(), 3);
    QCOMPARE(runner.calls.at(0).id, QStringLiteral("ui-autostart"));
    QCOMPARE(runner.calls.at(1).id, QStringLiteral("service"));
    QCOMPARE(runner.calls.at(2).id, QStringLiteral("fcitx"));
}

void TestOnboardingSetupController::canceledDownloadIsFailure()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);

    finishInitialNotReady(runner);
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

    finishInitialNotReady(runner);
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
    finishInitialNotReady(runner);
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
    finishInitialNotReady(runner);
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

void TestOnboardingSetupController::processRunnerTimesOutAndReusesId()
{
    QTest::failOnWarning(QRegularExpression(
        QStringLiteral("QProcess: Destroyed while process.*running")));
    QProcessSetupCommandRunner runner(50);
    QSignalSpy spy(&runner, &SetupCommandRunner::finished);
    QElapsedTimer elapsed;
    elapsed.start();

    runner.run(QStringLiteral("slow"), QStringLiteral("/bin/sh"),
               {QStringLiteral("-c"),
                QStringLiteral("trap '' TERM; sleep 5")});

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 1000);
    QVERIFY(elapsed.elapsed() < 1000);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("slow"));
    QCOMPARE(spy.first().at(1).toBool(), false);
    QVERIFY(spy.first().at(2).toString().contains(
        QStringLiteral("timed out after 50 ms")));

    runner.run(QStringLiteral("slow"), QStringLiteral("/bin/sh"),
               {QStringLiteral("-c"), QStringLiteral("exit 0")});
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 1000);
    QCOMPARE(spy.at(1).at(1).toBool(), true);
    QTest::qWait(100);
    QCOMPARE(spy.count(), 2);
}

void TestOnboardingSetupController::modelAdapterDetectsPresentModel()
{
    QTemporaryDir dir;
    const QString modelDir = dir.filePath(QStringLiteral("qwen3-asr-0.6b"));
    QVERIFY(QDir().mkpath(modelDir));
    QFile modelFile(modelDir + QStringLiteral("/qwen3-asr-0.6b-q4_k.gguf"));
    QVERIFY(modelFile.open(QIODevice::WriteOnly));
    modelFile.close();

    ModelSetupAdapter adapter(dir.path(), QStringLiteral("official"),
                              [](const QString &) { return echoflow::DownloadSnapshot{}; },
                              [](const echoflow::ModelEntry &, const QString &,
                                 const QString &) {});

    QVERIFY(adapter.modelPresent());
}

void TestOnboardingSetupController::modelAdapterDetectsMissingModel()
{
    QTemporaryDir dir;
    ModelSetupAdapter adapter(dir.path(), QStringLiteral("official"),
                              [](const QString &) { return echoflow::DownloadSnapshot{}; },
                              [](const echoflow::ModelEntry &, const QString &,
                                 const QString &) {});

    QVERIFY(!adapter.modelPresent());
}

void TestOnboardingSetupController::modelAdapterMapsRetainedDownloadingSnapshot()
{
    QString requestedId;
    ModelSetupAdapter adapter(QStringLiteral("/unused"), QStringLiteral("official"),
                              [&requestedId](const QString &id) {
                                  requestedId = id;
                                  echoflow::DownloadSnapshot snapshot;
                                  snapshot.state = echoflow::DownloadState::Downloading;
                                  return snapshot;
                              },
                              [](const echoflow::ModelEntry &, const QString &,
                                 const QString &) {});

    QVERIFY(adapter.downloadRunning());
    QCOMPARE(requestedId, QStringLiteral("qwen3-asr-0.6b"));
}

void TestOnboardingSetupController::modelAdapterStartsDefaultModel_data()
{
    QTest::addColumn<QString>("mirror");
    QTest::addColumn<QString>("expectedBaseUrl");

    QTest::newRow("official") << QStringLiteral("official")
                              << QStringLiteral("https://huggingface.co");
    QTest::newRow("hf-mirror") << QStringLiteral("hf-mirror")
                               << QStringLiteral("https://hf-mirror.com");
    QTest::newRow("default-empty") << QString()
                                   << QStringLiteral("https://hf-mirror.com");
}

void TestOnboardingSetupController::modelAdapterStartsDefaultModel()
{
    QFETCH(QString, mirror);
    QFETCH(QString, expectedBaseUrl);
    QTemporaryDir dir;
    int starts = 0;
    echoflow::ModelEntry startedEntry;
    QString startedDir;
    QString startedBaseUrl;
    ModelSetupAdapter adapter(
        dir.path(), mirror,
        [](const QString &) { return echoflow::DownloadSnapshot{}; },
        [&](const echoflow::ModelEntry &entry, const QString &targetDir,
            const QString &baseUrl) {
            ++starts;
            startedEntry = entry;
            startedDir = targetDir;
            startedBaseUrl = baseUrl;
        });

    adapter.startDownload();

    QCOMPARE(starts, 1);
    QCOMPARE(QString::fromStdString(startedEntry.id),
             QStringLiteral("qwen3-asr-0.6b"));
    QCOMPARE(QString::fromStdString(startedEntry.repo),
             QStringLiteral("cstr/qwen3-asr-0.6b-GGUF"));
    QCOMPARE(QString::fromStdString(startedEntry.displayName),
             QStringLiteral("Qwen3-ASR-0.6B (Q4_K GGUF)"));
    QCOMPARE(startedEntry.files.size(), std::size_t(1));
    QCOMPARE(QString::fromStdString(startedEntry.files.front()),
             QStringLiteral("qwen3-asr-0.6b-q4_k.gguf"));
    QCOMPARE(startedDir, dir.filePath(QStringLiteral("qwen3-asr-0.6b")));
    QCOMPARE(startedBaseUrl, expectedBaseUrl);
}

void TestOnboardingSetupController::modelAdapterRefreshesMirrorBeforeEachStart()
{
    QString mirror = QStringLiteral("hf-mirror");
    QStringList baseUrls;
    ModelSetupAdapter adapter(
        QStringLiteral("/unused"), [&mirror] { return mirror; },
        [](const QString &) { return echoflow::DownloadSnapshot{}; },
        [&baseUrls](const echoflow::ModelEntry &, const QString &,
                    const QString &baseUrl) { baseUrls.append(baseUrl); });

    adapter.startDownload();
    mirror = QStringLiteral("official");
    adapter.startDownload();

    QCOMPARE(baseUrls,
             QStringList({QStringLiteral("https://hf-mirror.com"),
                          QStringLiteral("https://huggingface.co")}));
}

void TestOnboardingSetupController::modelAdapterRejectsEmptyDependencies()
{
    const ModelSetupAdapter::MirrorProvider mirror =
        [] { return QStringLiteral("official"); };
    const ModelSetupAdapter::SnapshotProvider snapshot =
        [](const QString &) { return echoflow::DownloadSnapshot{}; };
    const ModelSetupAdapter::StartDownload start =
        [](const echoflow::ModelEntry &, const QString &, const QString &) {};

    QVERIFY_EXCEPTION_THROWN(
        ModelSetupAdapter(QStringLiteral("/unused"),
                          ModelSetupAdapter::MirrorProvider{}, snapshot, start),
        std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(
        ModelSetupAdapter(QStringLiteral("/unused"), mirror,
                          ModelSetupAdapter::SnapshotProvider{}, start),
        std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(
        ModelSetupAdapter(QStringLiteral("/unused"), mirror, snapshot,
                          ModelSetupAdapter::StartDownload{}),
        std::invalid_argument);
}

void TestOnboardingSetupController::modelAdapterFiltersCoordinatorSignals()
{
    ModelSetupAdapter adapter(QStringLiteral("/unused"), QStringLiteral("official"),
                              [](const QString &) { return echoflow::DownloadSnapshot{}; },
                              [](const echoflow::ModelEntry &, const QString &,
                                 const QString &) {});
    auto *coordinator = echoflow::ModelDownloadCoordinator::instance();
    adapter.observeCoordinator(coordinator);
    QSignalSpy progressSpy(&adapter, &ModelSetupSource::progress);
    QSignalSpy finishedSpy(&adapter, &ModelSetupSource::finished);

    emit coordinator->progress(QStringLiteral("qwen3-asr-1.7b"), 1, 10,
                               QStringLiteral("other.gguf"));
    emit coordinator->stateChanged(QStringLiteral("qwen3-asr-1.7b"),
                                   echoflow::DownloadState::Failed,
                                   QStringLiteral("other failed"));
    emit coordinator->progress(QStringLiteral("qwen3-asr-0.6b"), 4, 10,
                               QStringLiteral("default.gguf"));
    emit coordinator->stateChanged(QStringLiteral("qwen3-asr-0.6b"),
                                   echoflow::DownloadState::Downloading, {});
    emit coordinator->stateChanged(QStringLiteral("qwen3-asr-0.6b"),
                                   echoflow::DownloadState::Idle, {});
    emit coordinator->stateChanged(QStringLiteral("qwen3-asr-0.6b"),
                                   echoflow::DownloadState::Succeeded, {});
    emit coordinator->stateChanged(QStringLiteral("qwen3-asr-0.6b"),
                                   echoflow::DownloadState::Failed,
                                   QStringLiteral("network error"));

    QCOMPARE(progressSpy.count(), 1);
    QCOMPARE(progressSpy.first().at(0).toLongLong(), 4);
    QCOMPARE(progressSpy.first().at(1).toLongLong(), 10);
    QCOMPARE(finishedSpy.count(), 2);
    QCOMPARE(finishedSpy.at(0).at(0).toBool(), true);
    QVERIFY(finishedSpy.at(0).at(1).toString().isEmpty());
    QCOMPARE(finishedSpy.at(1).at(0).toBool(), false);
    QCOMPARE(finishedSpy.at(1).at(1).toString(), QStringLiteral("network error"));
}

void TestOnboardingSetupController::modelAdapterObservationIsIdempotentAndSwitchable()
{
    ModelSetupAdapter adapter(QStringLiteral("/unused"), QStringLiteral("official"),
                              [](const QString &) { return echoflow::DownloadSnapshot{}; },
                              [](const echoflow::ModelEntry &, const QString &,
                                 const QString &) {});
    auto *coordinator = echoflow::ModelDownloadCoordinator::instance();
    QSignalSpy progressSpy(&adapter, &ModelSetupSource::progress);

    adapter.observeCoordinator(coordinator);
    adapter.observeCoordinator(coordinator);
    emit coordinator->progress(QStringLiteral("qwen3-asr-0.6b"), 1, 3, {});
    QCOMPARE(progressSpy.count(), 1);

    adapter.observeCoordinator(nullptr);
    emit coordinator->progress(QStringLiteral("qwen3-asr-0.6b"), 2, 3, {});
    QCOMPARE(progressSpy.count(), 1);

    adapter.observeCoordinator(coordinator);
    emit coordinator->progress(QStringLiteral("qwen3-asr-0.6b"), 3, 3, {});
    QCOMPARE(progressSpy.count(), 2);
}

QTEST_GUILESS_MAIN(TestOnboardingSetupController)

#include "test_onboarding_setup_controller.moc"
