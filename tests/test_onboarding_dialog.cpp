// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "OnboardingDialog.h"
#include "OnboardingSetupController.h"
#include "OnboardingState.h"
#include "SetupCommandRunner.h"

#include <QFile>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
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

static void finishSuccessfulCommands(FakeCommandRunner &runner)
{
    runner.finish(QStringLiteral("ui-autostart"), true);
    runner.finish(QStringLiteral("service"), true);
    runner.finish(QStringLiteral("fcitx"), true);
    runner.finish(QStringLiteral("ui-autostart-check"), true);
    runner.finish(QStringLiteral("service-check"), true);
}

static QPushButton *button(OnboardingDialog &dialog, const char *name)
{
    auto *result = dialog.findChild<QPushButton *>(QString::fromLatin1(name));
    Q_ASSERT(result);
    return result;
}

class TestOnboardingDialog : public QObject {
    Q_OBJECT
private slots:
    void hasFourPagesAndBoundedNavigation();
    void startRunsSetupAndDisablesPrimaryAction();
    void rendersDeterminateAndIndeterminateModelProgress();
    void failureShowsErrorAndRetriesFailedWork();
    void aggregateFailureIsVisibleAndRetryable();
    void successChangesPrimaryActionToFinish();
    void closeDoesNotCompleteSetup();
    void incompleteWorkResumesOnFinalPage();
    void replayStartsAtFirstPageAndCompletedSetupDoesNotRerun();
};

void TestOnboardingDialog::hasFourPagesAndBoundedNavigation()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);
    OnboardingDialog dialog(&controller);

    auto *pages = dialog.findChild<QStackedWidget *>(QStringLiteral("pages"));
    auto *back = button(dialog, "backButton");
    auto *next = button(dialog, "nextButton");
    auto *indicator = dialog.findChild<QLabel *>(QStringLiteral("pageIndicator"));
    QVERIFY(pages);
    QVERIFY(indicator);
    QCOMPARE(pages->count(), 4);
    QCOMPARE(dialog.currentPage(), 0);
    QVERIFY(!back->isEnabled());
    QCOMPARE(next->text(), QStringLiteral("Next"));
    QCOMPARE(indicator->text(), QStringLiteral("1 / 4"));

    QTest::mouseClick(back, Qt::LeftButton);
    QCOMPARE(dialog.currentPage(), 0);
    next->setFocus();
    QTest::keyClick(next, Qt::Key_Space);
    QCOMPARE(dialog.currentPage(), 1);
    for (int page = 2; page < 4; ++page) {
        QTest::mouseClick(next, Qt::LeftButton);
        QCOMPARE(dialog.currentPage(), page);
    }
    QCOMPARE(next->text(), QStringLiteral("Start"));
    QCOMPARE(indicator->text(), QStringLiteral("4 / 4"));
    QTest::mouseClick(back, Qt::LeftButton);
    QCOMPARE(dialog.currentPage(), 2);
    QCOMPARE(next->text(), QStringLiteral("Next"));
}

void TestOnboardingDialog::startRunsSetupAndDisablesPrimaryAction()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);
    OnboardingDialog dialog(&controller);
    dialog.showForIncompleteSetup();
    auto *next = button(dialog, "nextButton");
    for (int i = 0; i < 3; ++i) {
        QTest::mouseClick(next, Qt::LeftButton);
    }

    QTest::mouseClick(next, Qt::LeftButton);
    QCOMPARE(model.starts, 1);
    QCOMPARE(runner.calls.size(), 3);
    QVERIFY(controller.isRunning());
    QVERIFY(!next->isEnabled());
    QCOMPARE(next->text(), QStringLiteral("Preparing..."));
}

void TestOnboardingDialog::rendersDeterminateAndIndeterminateModelProgress()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);
    OnboardingDialog dialog(&controller);
    dialog.showForIncompleteSetup();
    auto *next = button(dialog, "nextButton");
    for (int i = 0; i < 3; ++i) {
        QTest::mouseClick(next, Qt::LeftButton);
    }
    QTest::mouseClick(next, Qt::LeftButton);

    auto *progress = dialog.findChild<QProgressBar *>(QStringLiteral("modelProgressBar"));
    auto *label = dialog.findChild<QLabel *>(QStringLiteral("modelProgressLabel"));
    QVERIFY(progress);
    QVERIFY(label);
    model.reportProgress(5 * 1024 * 1024, 10 * 1024 * 1024);
    QCOMPARE(progress->maximum(), 1000);
    QCOMPARE(progress->value(), 500);
    QVERIFY(label->text().contains(QStringLiteral("5.0 MB")));
    QVERIFY(label->text().contains(QStringLiteral("10.0 MB")));

    model.reportProgress(7 * 1024 * 1024, 0);
    QCOMPARE(progress->minimum(), 0);
    QCOMPARE(progress->maximum(), 0);
    QVERIFY(label->text().contains(QStringLiteral("7.0 MB downloaded")));
}

void TestOnboardingDialog::failureShowsErrorAndRetriesFailedWork()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);
    OnboardingDialog dialog(&controller);
    dialog.showForIncompleteSetup();
    auto *next = button(dialog, "nextButton");
    for (int i = 0; i < 4; ++i) {
        QTest::mouseClick(next, Qt::LeftButton);
    }
    model.finish(false, QStringLiteral("Mirror timed out"));
    finishSuccessfulCommands(runner);

    auto *error = dialog.findChild<QLabel *>(QStringLiteral("modelErrorLabel"));
    QVERIFY(error);
    QCOMPARE(error->text(), QStringLiteral("Mirror timed out"));
    QVERIFY(error->isVisibleTo(&dialog));
    QCOMPARE(next->text(), QStringLiteral("Retry"));
    QVERIFY(next->isEnabled());

    QTest::mouseClick(next, Qt::LeftButton);
    QCOMPARE(model.starts, 2);
    QCOMPARE(next->text(), QStringLiteral("Preparing..."));
}

void TestOnboardingDialog::aggregateFailureIsVisibleAndRetryable()
{
    QTemporaryDir dir;
    const QString blockingFile = dir.filePath(QStringLiteral("not-a-directory"));
    QFile file(blockingFile);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    OnboardingState state(blockingFile + QStringLiteral("/ui-state.ini"));
    FakeModelSource model;
    model.present = true;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);
    OnboardingDialog dialog(&controller);
    dialog.showForIncompleteSetup();
    auto *next = button(dialog, "nextButton");
    for (int i = 0; i < 4; ++i) {
        QTest::mouseClick(next, Qt::LeftButton);
    }
    finishSuccessfulCommands(runner);

    auto *error = dialog.findChild<QLabel *>(QStringLiteral("aggregateErrorLabel"));
    QVERIFY(error);
    QVERIFY(error->text().contains(QStringLiteral("Failed to create")));
    QCOMPARE(next->text(), QStringLiteral("Retry"));
    const int callsBeforeRetry = runner.calls.size();
    QTest::mouseClick(next, Qt::LeftButton);
    QCOMPARE(runner.calls.size(), callsBeforeRetry);
    QCOMPARE(next->text(), QStringLiteral("Retry"));
}

void TestOnboardingDialog::successChangesPrimaryActionToFinish()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);
    OnboardingDialog dialog(&controller);
    QSignalSpy finishedSpy(&dialog,
                           &OnboardingDialog::finishedAndSettingsRequested);
    dialog.showForIncompleteSetup();
    auto *next = button(dialog, "nextButton");
    for (int i = 0; i < 4; ++i) {
        QTest::mouseClick(next, Qt::LeftButton);
    }
    model.finish(true);
    finishSuccessfulCommands(runner);

    QCOMPARE(next->text(), QStringLiteral("Finish"));
    QVERIFY(next->isEnabled());
    QTest::mouseClick(next, Qt::LeftButton);
    QCOMPARE(finishedSpy.count(), 1);
}

void TestOnboardingDialog::closeDoesNotCompleteSetup()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);
    OnboardingDialog dialog(&controller);
    dialog.showForIncompleteSetup();

    dialog.close();
    QVERIFY(!dialog.isVisible());
    QVERIFY(!controller.hasStarted());
    QVERIFY(!state.isComplete());

    dialog.showForIncompleteSetup();
    QTest::keyClick(&dialog, Qt::Key_Escape);
    QVERIFY(!dialog.isVisible());
    QVERIFY(!controller.hasStarted());
    QVERIFY(!controller.isComplete());
    QVERIFY(!state.isComplete());
}

void TestOnboardingDialog::incompleteWorkResumesOnFinalPage()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);
    controller.start();
    OnboardingDialog dialog(&controller);

    dialog.showForIncompleteSetup();
    QCOMPARE(dialog.currentPage(), 3);
    QCOMPARE(button(dialog, "nextButton")->text(),
             QStringLiteral("Preparing..."));
    QCOMPARE(model.starts, 1);
}

void TestOnboardingDialog::replayStartsAtFirstPageAndCompletedSetupDoesNotRerun()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    QVERIFY(state.markComplete());
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);
    OnboardingDialog dialog(&controller);

    dialog.showForReplay();
    QCOMPARE(dialog.currentPage(), 0);
    auto *next = button(dialog, "nextButton");
    for (int i = 0; i < 3; ++i) {
        QTest::mouseClick(next, Qt::LeftButton);
    }
    QCOMPARE(next->text(), QStringLiteral("Finish"));
    QTest::mouseClick(next, Qt::LeftButton);
    QCOMPARE(model.starts, 0);
    QVERIFY(runner.calls.isEmpty());
}

QTEST_MAIN(TestOnboardingDialog)

#include "test_onboarding_dialog.moc"
