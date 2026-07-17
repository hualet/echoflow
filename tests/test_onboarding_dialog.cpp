// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "OnboardingDialog.h"
#include "OnboardingSetupController.h"
#include "OnboardingState.h"
#include "SetupCommandRunner.h"

#include <QFile>
#include <QIcon>
#include <QLabel>
#include <QPalette>
#include <QPixmap>
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

static void finishInitialNotReady(FakeCommandRunner &runner)
{
    runner.finish(QStringLiteral("ui-autostart-initial-check"), false,
                  QStringLiteral("disabled"));
    runner.finish(QStringLiteral("service-initial-check"), false,
                  QStringLiteral("inactive"));
    runner.calls.clear();
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
    void usesApprovedChineseCopyAndAccessiblePresentation();
    void startRunsSetupAndDisablesPrimaryAction();
    void rendersDeterminateAndIndeterminateModelProgress();
    void failureShowsErrorAndRetriesFailedWork();
    void aggregateFailureIsVisibleAndRetryable();
    void successChangesPrimaryActionToFinish();
    void closeDoesNotCompleteSetup();
    void readOnlyReconstructionDoesNotCountAsStarted();
    void incompleteWorkResumesOnFinalPage();
    void failedSetupReopensOnFinalPageWithRetry();
    void replayStartsAtFirstPageAndCompletedSetupDoesNotRerun();
};

void TestOnboardingDialog::hasFourPagesAndBoundedNavigation()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);
    finishInitialNotReady(runner);
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
    QCOMPARE(back->text(), QStringLiteral("上一步"));
    QCOMPARE(next->text(), QStringLiteral("下一步"));
    QCOMPARE(indicator->text(), QStringLiteral("第 1 页，共 4 页"));

    QTest::mouseClick(back, Qt::LeftButton);
    QCOMPARE(dialog.currentPage(), 0);
    next->setFocus();
    QTest::keyClick(next, Qt::Key_Space);
    QCOMPARE(dialog.currentPage(), 1);
    for (int page = 2; page < 4; ++page) {
        QTest::mouseClick(next, Qt::LeftButton);
        QCOMPARE(dialog.currentPage(), page);
    }
    QCOMPARE(next->text(), QStringLiteral("开始使用 EchoFlow"));
    QCOMPARE(indicator->text(), QStringLiteral("第 4 页，共 4 页"));
    QTest::mouseClick(back, Qt::LeftButton);
    QCOMPARE(dialog.currentPage(), 2);
    QCOMPARE(next->text(), QStringLiteral("下一步"));
}

void TestOnboardingDialog::usesApprovedChineseCopyAndAccessiblePresentation()
{
    const QIcon previousIcon = QApplication::windowIcon();
    QPixmap iconPixmap(12, 12);
    iconPixmap.fill(Qt::cyan);
    const QIcon testIcon(iconPixmap);
    QVERIFY(!testIcon.isNull());
    QApplication::setWindowIcon(testIcon);

    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);
    finishInitialNotReady(runner);
    OnboardingDialog dialog(&controller);

    QCOMPARE(dialog.windowIcon().cacheKey(), testIcon.cacheKey());
    const QList<QPair<QString, QString>> copy = {
        {QStringLiteral("introHeading"), QStringLiteral("离线、安全、流畅")},
        {QStringLiteral("shortcutHeading"),
         QStringLiteral("按右 Ctrl 键开始说话")},
        {QStringLiteral("settingsHeading"), QStringLiteral("从托盘打开设置")},
        {QStringLiteral("setupHeading"), QStringLiteral("准备开始使用")},
        {QStringLiteral("introDescriptionLabel"),
         QStringLiteral("语音识别在本机离线运行；首次使用需要联网下载模型。录音不会离开你的设备。")},
        {QStringLiteral("shortcutDescriptionLabel"),
         QStringLiteral("第一次按下右 Ctrl 键开始录音，第二次按下停止录音。")},
        {QStringLiteral("shortcutTranscriptLabel"),
         QStringLiteral("识别完成后，文字会自动输入到当前聚焦的文本框。")},
        {QStringLiteral("settingsOptionsLabel"),
         QStringLiteral("在设置中选择模型、语言、麦克风和下载镜像。")},
        {QStringLiteral("settingsReplayLabel"),
         QStringLiteral("你也可以随时从托盘重播本使用指南。")},
    };
    for (const auto &[objectName, expected] : copy) {
        auto *label = dialog.findChild<QLabel *>(objectName);
        QVERIFY2(label, qPrintable(objectName));
        QCOMPARE(label->text(), expected);
        QVERIFY(label->wordWrap());
    }

    auto *description =
        dialog.findChild<QLabel *>(QStringLiteral("introDescriptionLabel"));
    QVERIFY(description);
    QVERIFY(description->wordWrap());
    QPalette palette = dialog.palette();
    palette.setColor(QPalette::WindowText, QColor(12, 34, 56));
    dialog.setPalette(palette);
    QApplication::processEvents();
    QCOMPARE(description->palette().color(QPalette::WindowText),
             palette.color(QPalette::WindowText));

    for (const QString &objectName : {
             QStringLiteral("backButton"), QStringLiteral("nextButton"),
             QStringLiteral("pageIndicator"),
             QStringLiteral("modelStatusLabel"),
             QStringLiteral("modelProgressBar"),
             QStringLiteral("serviceStatusLabel"),
             QStringLiteral("fcitxStatusLabel")}) {
        auto *widget = dialog.findChild<QWidget *>(objectName);
        QVERIFY2(widget, qPrintable(objectName));
        QVERIFY2(!widget->accessibleName().isEmpty(), qPrintable(objectName));
    }

    QApplication::setWindowIcon(previousIcon);
}

void TestOnboardingDialog::startRunsSetupAndDisablesPrimaryAction()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);
    finishInitialNotReady(runner);
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
    QCOMPARE(next->text(), QStringLiteral("准备中…"));
}

void TestOnboardingDialog::rendersDeterminateAndIndeterminateModelProgress()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);
    finishInitialNotReady(runner);
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
    QCOMPARE(label->text(), QStringLiteral("已下载 5.0 MB，共 10.0 MB"));

    model.reportProgress(7 * 1024 * 1024, 0);
    QCOMPARE(progress->minimum(), 0);
    QCOMPARE(progress->maximum(), 0);
    QCOMPARE(label->text(), QStringLiteral("已下载 7.0 MB"));
}

void TestOnboardingDialog::failureShowsErrorAndRetriesFailedWork()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);
    finishInitialNotReady(runner);
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
    QCOMPARE(next->text(), QStringLiteral("重试"));
    QVERIFY(next->isEnabled());

    QTest::mouseClick(next, Qt::LeftButton);
    QCOMPARE(model.starts, 2);
    QCOMPARE(next->text(), QStringLiteral("准备中…"));
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
    finishInitialNotReady(runner);
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
    QCOMPARE(next->text(), QStringLiteral("重试"));
    const int callsBeforeRetry = runner.calls.size();
    QTest::mouseClick(next, Qt::LeftButton);
    QCOMPARE(runner.calls.size(), callsBeforeRetry);
    QCOMPARE(next->text(), QStringLiteral("重试"));
}

void TestOnboardingDialog::successChangesPrimaryActionToFinish()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);
    finishInitialNotReady(runner);
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

    QCOMPARE(next->text(), QStringLiteral("完成并打开设置"));
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
    finishInitialNotReady(runner);
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

void TestOnboardingDialog::readOnlyReconstructionDoesNotCountAsStarted()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);
    OnboardingDialog dialog(&controller);

    dialog.showForIncompleteSetup();
    QCOMPARE(dialog.currentPage(), 3);
    QVERIFY(!controller.hasStarted());
    dialog.close();

    finishInitialNotReady(runner);
    dialog.showForIncompleteSetup();
    QCOMPARE(dialog.currentPage(), 0);
    QVERIFY(!controller.hasStarted());
}

void TestOnboardingDialog::incompleteWorkResumesOnFinalPage()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);
    controller.start();
    finishInitialNotReady(runner);
    OnboardingDialog dialog(&controller);

    dialog.showForIncompleteSetup();
    QCOMPARE(dialog.currentPage(), 3);
    QCOMPARE(button(dialog, "nextButton")->text(),
             QStringLiteral("准备中…"));
    QCOMPARE(model.starts, 1);
}

void TestOnboardingDialog::failedSetupReopensOnFinalPageWithRetry()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);
    finishInitialNotReady(runner);
    {
        OnboardingDialog firstDialog(&controller);
        firstDialog.showForIncompleteSetup();
        auto *next = button(firstDialog, "nextButton");
        for (int i = 0; i < 4; ++i) {
            QTest::mouseClick(next, Qt::LeftButton);
        }
        model.finish(false, QStringLiteral("下载失败"));
        finishSuccessfulCommands(runner);
        QVERIFY(!controller.isRunning());
        QCOMPARE(controller.itemState(SetupItem::Model), SetupItemState::Failed);
    }

    OnboardingDialog reopenedDialog(&controller);
    reopenedDialog.showForIncompleteSetup();
    QCOMPARE(reopenedDialog.currentPage(), 3);
    QCOMPARE(button(reopenedDialog, "nextButton")->text(),
             QStringLiteral("重试"));
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
    QCOMPARE(next->text(), QStringLiteral("完成并打开设置"));
    QTest::mouseClick(next, Qt::LeftButton);
    QCOMPARE(model.starts, 0);
    QVERIFY(runner.calls.isEmpty());
}

QTEST_MAIN(TestOnboardingDialog)

#include "test_onboarding_dialog.moc"
