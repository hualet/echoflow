// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "OnboardingDialog.h"
#include "OnboardingIllustration.h"
#include "OnboardingSetupController.h"
#include "OnboardingState.h"
#include "SetupCommandRunner.h"

#include <DTitlebar>

#include <QAccessible>
#include <QFile>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QPalette>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QRegion>
#include <QScrollArea>
#include <QScrollBar>
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

static QImage renderIllustration(OnboardingIllustration &illustration,
                                 qreal devicePixelRatio)
{
    const QSize logicalSize(240, 180);
    illustration.resize(logicalSize);
    illustration.show();
    QApplication::processEvents();

    QImage image(QSize(qRound(logicalSize.width() * devicePixelRatio),
                       qRound(logicalSize.height() * devicePixelRatio)),
                 QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(devicePixelRatio);
    image.fill(Qt::transparent);
    illustration.render(&image, QPoint(), QRegion(), QWidget::DrawChildren);
    return image;
}

static QRect pixelBounds(const QImage &image, bool redOnly)
{
    QRect bounds;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor color = image.pixelColor(x, y);
            const bool matches = color.alpha() > 0
                && (!redOnly || (color.red() > 200 && color.green() < 80
                                 && color.blue() < 80));
            if (matches) {
                bounds |= QRect(x, y, 1, 1);
            }
        }
    }
    return bounds;
}

class TestOnboardingDialog : public QObject {
    Q_OBJECT
private slots:
    void bundlesIllustrationResources();
    void illustrationPreservesSourceAndCollapsesMissingResource();
    void hasFourPagesAndBoundedNavigation();
    void usesApprovedVisualStoryAndAccessibleImages();
    void setupErrorsRemainReachableAtMinimumSize();
    void startRunsSetupAndDisablesPrimaryAction();
    void rendersDeterminateAndIndeterminateModelProgress();
    void failureShowsErrorAndRetriesFailedWork();
    void aggregateFailureIsVisibleAndRetryable();
    void successChangesPrimaryActionToFinish();
    void closeDoesNotCompleteSetup();
    void readOnlyProbesStartAtIntroAndQueuedSetupResumes();
    void incompleteWorkResumesOnFinalPage();
    void failedSetupReopensOnFinalPageWithRetry();
    void replayStartsAtFirstPageAndCompletedSetupDoesNotRerun();
};

void TestOnboardingDialog::bundlesIllustrationResources()
{
    for (const QString &path : {
             QStringLiteral(":/onboarding/intro.png"),
             QStringLiteral(":/onboarding/shortcut.png"),
             QStringLiteral(":/onboarding/settings.png"),
             QStringLiteral(":/onboarding/setup.png")}) {
        const QPixmap illustration(path);
        QVERIFY2(!illustration.isNull(), qPrintable(path));
    }
}

void TestOnboardingDialog::illustrationPreservesSourceAndCollapsesMissingResource()
{
    OnboardingIllustration valid(QStringLiteral(":/onboarding/intro.png"),
                                 QStringLiteral("本机离线语音识别示意图"));
    QVERIFY(!valid.pixmap(Qt::ReturnByValue).isNull());
    QCOMPARE(valid.pixmap(Qt::ReturnByValue).size(), QSize(768, 768));
    valid.resize(140, 210);
    QApplication::processEvents();
    QCOMPARE(valid.pixmap(Qt::ReturnByValue).size(), QSize(768, 768));

    valid.clear();
    QVERIFY(pixelBounds(renderIllustration(valid, 1.0), false).isNull());
    QCOMPARE(valid.sizeHint(), QSize(0, 0));
    QCOMPARE(valid.minimumSizeHint(), QSize(0, 0));

    QPixmap replacement(80, 40);
    replacement.fill(QColor(240, 20, 20));
    valid.setPixmap(replacement);
    QCOMPARE(valid.pixmap(Qt::ReturnByValue).size(), QSize(80, 40));
    for (const qreal devicePixelRatio : {1.0, 2.0}) {
        const QImage image = renderIllustration(valid, devicePixelRatio);
        const QRect bounds = pixelBounds(image, true);
        QVERIFY(!bounds.isNull());
        QVERIFY(qAbs(bounds.width()
                     - qRound(240 * devicePixelRatio)) <= 2);
        QVERIFY(qAbs(bounds.height()
                     - qRound(120 * devicePixelRatio)) <= 2);
        QVERIFY(qAbs((bounds.left() + bounds.right())
                     - (image.rect().left() + image.rect().right())) <= 2);
        QVERIFY(qAbs((bounds.top() + bounds.bottom())
                     - (image.rect().top() + image.rect().bottom())) <= 2);
        QVERIFY(image.rect().contains(bounds));
    }

    QWidget page;
    auto *layout = new QHBoxLayout(&page);
    auto *missing = new OnboardingIllustration(
        QStringLiteral(":/onboarding/does-not-exist.png"),
        QStringLiteral("不应暴露的替代文本"), &page);
    auto *copy = new QLabel(QStringLiteral("原生说明文字"), &page);
    layout->addWidget(missing);
    layout->addWidget(copy);
    page.show();
    QApplication::processEvents();

    QVERIFY(missing->pixmap(Qt::ReturnByValue).isNull());
    QVERIFY(missing->isHidden());
    QVERIFY(missing->accessibleName().isEmpty());
    QCOMPARE(missing->minimumSize(), QSize(0, 0));
    QCOMPARE(missing->minimumSizeHint(), QSize(0, 0));
    QCOMPARE(missing->sizePolicy().horizontalPolicy(), QSizePolicy::Ignored);
    QCOMPARE(missing->sizePolicy().verticalPolicy(), QSizePolicy::Ignored);
    QVERIFY(copy->isVisibleTo(&page));
}

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
    auto *dots = dialog.findChild<QWidget *>(QStringLiteral("pageDots"));
    QVERIFY(pages);
    QVERIFY(dots);
    QCOMPARE(pages->count(), 4);
    QCOMPARE(dots->findChildren<QWidget *>(QString(),
                                           Qt::FindDirectChildrenOnly)
                 .size(),
             0);
    QAccessibleInterface *dotsInterface =
        QAccessible::queryAccessibleInterface(dots);
    QVERIFY(dotsInterface);
    QCOMPARE(dotsInterface->childCount(), 0);
    QCOMPARE(dots->property("activePage").toInt(), 0);
    QCOMPARE(dialog.currentPage(), 0);
    QVERIFY(!back->isEnabled());
    QCOMPARE(back->text(), QStringLiteral("上一步"));
    QCOMPARE(next->text(), QStringLiteral("下一步"));
    QCOMPARE(dots->accessibleName(), QStringLiteral("第 1 页，共 4 页"));
    dialog.resize(dialog.minimumSize());
    dialog.show();
    QApplication::processEvents();
    const int initialDotsCenter =
        dots->mapTo(&dialog, dots->rect().center()).x();

    QTest::mouseClick(back, Qt::LeftButton);
    QCOMPARE(dialog.currentPage(), 0);
    next->setFocus();
    QTest::keyClick(next, Qt::Key_Space);
    QCOMPARE(dialog.currentPage(), 1);
    QCOMPARE(dots->accessibleName(), QStringLiteral("第 2 页，共 4 页"));
    QCOMPARE(dots->property("activePage").toInt(), 1);
    for (int page = 2; page < 4; ++page) {
        QTest::mouseClick(next, Qt::LeftButton);
        QCOMPARE(dialog.currentPage(), page);
        QCOMPARE(dots->accessibleName(),
                 QStringLiteral("第 %1 页，共 4 页").arg(page + 1));
        QCOMPARE(dots->property("activePage").toInt(), page);
    }
    QCOMPARE(next->text(), QStringLiteral("开始使用 EchoFlow"));
    QApplication::processEvents();
    const int finalDotsCenter =
        dots->mapTo(&dialog, dots->rect().center()).x();
    QVERIFY(qAbs(finalDotsCenter - initialDotsCenter) <= 1);
    QTest::mouseClick(back, Qt::LeftButton);
    QCOMPARE(dialog.currentPage(), 2);
    QCOMPARE(dots->accessibleName(), QStringLiteral("第 3 页，共 4 页"));
    QCOMPARE(next->text(), QStringLiteral("下一步"));
}

void TestOnboardingDialog::usesApprovedVisualStoryAndAccessibleImages()
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
    dialog.resize(dialog.minimumSize());
    dialog.show();
    QApplication::processEvents();

    QCOMPARE(dialog.windowIcon().cacheKey(), testIcon.cacheKey());
    auto *titlebar = dialog.findChild<Dtk::Widget::DTitlebar *>(
        QStringLiteral("onboardingTitlebar"));
    QVERIFY(titlebar);
    QCOMPARE(dialog.title(), QString());

    auto *titleLabel = titlebar->findChild<QLabel *>(
        QStringLiteral("onboardingTitleLabel"));
    auto *titleIcon = titlebar->findChild<QLabel *>(
        QStringLiteral("onboardingTitleIcon"));
    QVERIFY(titleLabel);
    QVERIFY(titleIcon);
    QCOMPARE(titleLabel->text(), QStringLiteral("欢迎使用 EchoFlow"));
    QCOMPARE(titleLabel->accessibleName(), QStringLiteral("欢迎使用 EchoFlow"));
    QVERIFY(!titleIcon->pixmap(Qt::ReturnByValue).isNull());
    QCOMPARE(titleIcon->accessibleName(), QStringLiteral("EchoFlow"));

    struct VisualPage {
        QString illustrationObjectName;
        QString illustrationAccessibleName;
        QString headingObjectName;
        QString heading;
        QString descriptionObjectName;
        QString description;
        QString tagObjectName;
        QString tag;
    };
    const QList<VisualPage> pages = {
        {QStringLiteral("introIllustration"),
         QStringLiteral("本机离线语音识别示意图"),
         QStringLiteral("introHeading"), QStringLiteral("说话，就能输入"),
         QStringLiteral("introDescriptionLabel"),
         QStringLiteral("语音识别在本机完成，录音不会离开你的设备。"),
         QStringLiteral("introTagLabel"),
         QStringLiteral("离线 · 隐私 · 快速")},
        {QStringLiteral("shortcutIllustration"),
         QStringLiteral("右 Ctrl 语音输入快捷键示意图"),
         QStringLiteral("shortcutHeading"),
         QStringLiteral("按右 Ctrl，开始说话"),
         QStringLiteral("shortcutDescriptionLabel"),
         QStringLiteral("再按一次结束，识别文字会直接进入当前输入框。"),
         QStringLiteral("shortcutTagLabel"),
         QStringLiteral("右 Ctrl · 开始 / 结束")},
        {QStringLiteral("settingsIllustration"),
         QStringLiteral("托盘与设置入口示意图"),
         QStringLiteral("settingsHeading"),
         QStringLiteral("需要调整？都在托盘里"),
         QStringLiteral("settingsDescriptionLabel"),
         QStringLiteral("切换模型、语言、麦克风，也可以随时重播这份指引。"),
         QStringLiteral("settingsTagLabel"),
         QStringLiteral("托盘 · 设置 · 使用指引")},
    };
    for (const VisualPage &page : pages) {
        auto *illustration = dialog.findChild<QLabel *>(
            page.illustrationObjectName);
        QVERIFY2(illustration,
                 qPrintable(page.illustrationObjectName));
        QVERIFY2(!illustration->pixmap(Qt::ReturnByValue).isNull(),
                 qPrintable(page.illustrationObjectName));
        QCOMPARE(illustration->pixmap(Qt::ReturnByValue).size(),
                 QSize(768, 768));
        illustration->resize(180, 220);
        QApplication::processEvents();
        QCOMPARE(illustration->pixmap(Qt::ReturnByValue).size(),
                 QSize(768, 768));
        QCOMPARE(illustration->accessibleName(),
                 page.illustrationAccessibleName);

        auto *heading = dialog.findChild<QLabel *>(page.headingObjectName);
        QVERIFY2(heading, qPrintable(page.headingObjectName));
        QCOMPARE(heading->text(), page.heading);

        auto *description =
            dialog.findChild<QLabel *>(page.descriptionObjectName);
        QVERIFY2(description, qPrintable(page.descriptionObjectName));
        QCOMPARE(description->text(), page.description);
        QVERIFY(description->wordWrap());

        auto *tag = dialog.findChild<QLabel *>(page.tagObjectName);
        QVERIFY2(tag, qPrintable(page.tagObjectName));
        QCOMPARE(tag->text(), page.tag);
        QVERIFY(!tag->wordWrap());
        QVERIFY(tag->property("semanticTag").toBool());
        QCOMPARE(tag->backgroundRole(), QPalette::AlternateBase);
        QCOMPARE(tag->foregroundRole(), QPalette::Text);
        QCOMPARE(tag->contentsMargins(), QMargins(10, 4, 10, 4));
        QCOMPARE(tag->property("cornerRadius").toInt(), 10);
        QCOMPARE(tag->sizePolicy().horizontalPolicy(), QSizePolicy::Maximum);
        QCOMPARE(tag->sizePolicy().verticalPolicy(), QSizePolicy::Preferred);
        auto *copyLayout = tag->parentWidget()->layout()->itemAt(1)->layout();
        QVERIFY(copyLayout);
        const int tagIndex = copyLayout->indexOf(tag);
        QVERIFY(tagIndex >= 0);
        QCOMPARE(copyLayout->itemAt(tagIndex)->alignment(), Qt::AlignLeft);
        QVERIFY(tag->width() <= tag->sizeHint().width());
        const QMargins margins = tag->contentsMargins();
        const QFontMetrics metrics(tag->font());
        QCOMPARE(tag->height(), tag->sizeHint().height());
        QVERIFY(tag->height() <= metrics.lineSpacing() + margins.top()
                                     + margins.bottom() + 1);
        QVERIFY(tag->contentsRect().width()
                >= metrics.horizontalAdvance(tag->text()));
    }

    auto *setupIllustration =
        dialog.findChild<QLabel *>(QStringLiteral("setupIllustration"));
    QVERIFY(setupIllustration);
    QVERIFY(!setupIllustration->pixmap(Qt::ReturnByValue).isNull());
    QCOMPARE(setupIllustration->accessibleName(),
             QStringLiteral("模型下载与服务准备示意图"));
    auto *setupHeading =
        dialog.findChild<QLabel *>(QStringLiteral("setupHeading"));
    QVERIFY(setupHeading);
    QCOMPARE(setupHeading->text(), QStringLiteral("准备好，就可以开始"));
    auto *setupDescription =
        dialog.findChild<QLabel *>(QStringLiteral("setupDescriptionLabel"));
    QVERIFY(setupDescription);
    QCOMPARE(setupDescription->text(),
             QStringLiteral("首次下载模型需要联网；服务和 Fcitx 会同步检查。"));

    auto *dots = dialog.findChild<QWidget *>(QStringLiteral("pageDots"));
    QVERIFY(dots);
    QVERIFY(!dots->accessibleName().isEmpty());
    QCOMPARE(dots->property("activePage").toInt(), 0);
    QCOMPARE(dots->findChildren<QWidget *>(QString(),
                                           Qt::FindDirectChildrenOnly)
                 .size(),
             0);
    QCOMPARE(dialog.minimumWidth(), 680);
    QVERIFY(dialog.minimumHeight() >= 500);

    auto *description =
        dialog.findChild<QLabel *>(QStringLiteral("introDescriptionLabel"));
    QVERIFY(description);
    QVERIFY(description->wordWrap());
    QPalette palette = dialog.palette();
    palette.setColor(QPalette::WindowText, QColor(12, 34, 56));
    palette.setColor(QPalette::AlternateBase, QColor(23, 45, 67));
    palette.setColor(QPalette::Text, QColor(78, 90, 12));
    dialog.setPalette(palette);
    QApplication::processEvents();
    QCOMPARE(description->palette().color(QPalette::WindowText),
             palette.color(QPalette::WindowText));
    for (const VisualPage &page : pages) {
        auto *tag = dialog.findChild<QLabel *>(page.tagObjectName);
        QVERIFY(tag);
        QCOMPARE(tag->palette().color(tag->backgroundRole()),
                 palette.color(QPalette::AlternateBase));
        QCOMPARE(tag->palette().color(tag->foregroundRole()),
                 palette.color(QPalette::Text));
    }

    for (const QString &objectName : {
             QStringLiteral("backButton"), QStringLiteral("nextButton"),
             QStringLiteral("pageDots"),
             QStringLiteral("modelStatusLabel"),
             QStringLiteral("modelProgressBar"),
             QStringLiteral("serviceStatusLabel"),
             QStringLiteral("fcitxStatusLabel")}) {
        auto *widget = dialog.findChild<QWidget *>(objectName);
        QVERIFY2(widget, qPrintable(objectName));
        QVERIFY2(!widget->accessibleName().isEmpty(), qPrintable(objectName));
    }

    auto *introImage =
        dialog.findChild<QLabel *>(QStringLiteral("introIllustration"));
    QVERIFY(introImage);
    introImage->clear();
    QApplication::processEvents();
    auto *introHeading =
        dialog.findChild<QLabel *>(QStringLiteral("introHeading"));
    QVERIFY(introHeading->isVisibleTo(&dialog));
    auto *next = button(dialog, "nextButton");
    QVERIFY(next->isVisibleTo(&dialog));
    QVERIFY(next->isEnabled());
    QTest::mouseClick(next, Qt::LeftButton);
    QCOMPARE(dialog.currentPage(), 1);

    QApplication::setWindowIcon(previousIcon);
}

void TestOnboardingDialog::setupErrorsRemainReachableAtMinimumSize()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);
    finishInitialNotReady(runner);
    OnboardingDialog dialog(&controller);
    dialog.resize(dialog.minimumSize());
    dialog.show();
    auto *next = button(dialog, "nextButton");
    for (int i = 0; i < 3; ++i) {
        QTest::mouseClick(next, Qt::LeftButton);
    }
    QApplication::processEvents();
    QCOMPARE(dialog.size(), QSize(680, 500));

    auto *scroll =
        dialog.findChild<QScrollArea *>(QStringLiteral("setupScrollArea"));
    QVERIFY(scroll);
    QVERIFY(scroll->widgetResizable());
    QCOMPARE(scroll->frameShape(), QFrame::NoFrame);
    QCOMPARE(scroll->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
    QCOMPARE(scroll->verticalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
    QCOMPARE(scroll->verticalScrollBar()->maximum(), 0);

    QTest::mouseClick(next, Qt::LeftButton);
    const QString modelError = QStringLiteral(
        "模型下载服务器暂时不可用，请检查网络连接。\n"
        "镜像返回了超时错误，已保留当前下载进度。\n"
        "稍后可以从此页面重试下载。");
    const QString serviceError = QStringLiteral(
        "无法启用托盘自启动，systemd 用户会话拒绝了请求。\n"
        "后台服务未能启动，请检查用户日志。\n"
        "修复会话后可以重新尝试。");
    const QString fcitxError = QStringLiteral(
        "Fcitx 重新加载失败，当前输入法连接仍不可用。\n"
        "请确认 Fcitx 进程正在运行。\n"
        "然后返回此页面重试。");
    model.finish(false, modelError);
    runner.finish(QStringLiteral("ui-autostart"), false,
                  serviceError.section(QLatin1Char('\n'), 0, 0));
    runner.finish(QStringLiteral("service"), false,
                  serviceError.section(QLatin1Char('\n'), 1));
    runner.finish(QStringLiteral("fcitx"), false, fcitxError);
    QApplication::processEvents();

    QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
    for (const QString &objectName : {
             QStringLiteral("modelErrorLabel"),
             QStringLiteral("serviceErrorLabel"),
             QStringLiteral("fcitxErrorLabel")}) {
        auto *error = dialog.findChild<QLabel *>(objectName);
        QVERIFY2(error, qPrintable(objectName));
        QVERIFY2(scroll->widget()->isAncestorOf(error), qPrintable(objectName));
        QVERIFY2(error->height() >= error->heightForWidth(error->width()),
                 qPrintable(objectName));
        scroll->ensureWidgetVisible(error, 0, 0);
        QApplication::processEvents();
        const QRect visibleRect = error->rect().translated(
            error->mapTo(scroll->viewport(), QPoint()));
        QVERIFY2(visibleRect.intersects(scroll->viewport()->rect()),
                 qPrintable(objectName));
    }
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

void TestOnboardingDialog::readOnlyProbesStartAtIntroAndQueuedSetupResumes()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);
    OnboardingDialog dialog(&controller);

    dialog.showForIncompleteSetup();
    QCOMPARE(dialog.currentPage(), 0);
    QVERIFY(!controller.hasStarted());
    auto *next = button(dialog, "nextButton");
    for (int i = 0; i < 3; ++i) {
        QTest::mouseClick(next, Qt::LeftButton);
    }
    QCOMPARE(dialog.currentPage(), 3);
    QCOMPARE(next->text(), QStringLiteral("准备中…"));
    QVERIFY(!next->isEnabled());

    controller.start();
    QVERIFY(controller.hasStarted());
    QCOMPARE(model.starts, 0);
    dialog.close();

    OnboardingDialog reopenedDialog(&controller);
    reopenedDialog.showForIncompleteSetup();
    QCOMPARE(reopenedDialog.currentPage(), 3);
    QCOMPARE(button(reopenedDialog, "nextButton")->text(),
             QStringLiteral("准备中…"));
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
