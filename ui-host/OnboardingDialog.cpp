// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "OnboardingDialog.h"

#include "OnboardingSetupController.h"

#include <QApplication>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QLabel *wrappedLabel(const QString &text, QWidget *parent,
                     const QString &objectName = {})
{
    auto *label = new QLabel(text, parent);
    if (!objectName.isEmpty()) {
        label->setObjectName(objectName);
    }
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

QLabel *pageTitle(const QString &text, QWidget *parent,
                  const QString &objectName)
{
    auto *label = wrappedLabel(text, parent, objectName);
    QFont font = label->font();
    font.setPointSize(font.pointSize() + 5);
    font.setBold(true);
    label->setFont(font);
    return label;
}

QWidget *informationPage(
    const QString &title, const QString &titleObjectName, const QString &body,
    const QString &bodyObjectName,
    const QList<QPair<QString, QString>> &points)
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(14);
    layout->addWidget(pageTitle(title, page, titleObjectName));
    layout->addWidget(wrappedLabel(body, page, bodyObjectName));
    for (const auto &[point, objectName] : points) {
        auto *label = wrappedLabel(point, page, objectName);
        label->setContentsMargins(8, 0, 8, 0);
        layout->addWidget(label);
    }
    layout->addStretch();
    return page;
}

QString stateText(SetupItemState state)
{
    switch (state) {
    case SetupItemState::Pending:
        return QStringLiteral("等待中");
    case SetupItemState::Running:
        return QStringLiteral("进行中");
    case SetupItemState::Succeeded:
        return QStringLiteral("已完成");
    case SetupItemState::Failed:
        return QStringLiteral("失败");
    }
    return {};
}

SetupItemState combinedServiceState(OnboardingSetupController *controller)
{
    const SetupItemState autostart =
        controller->itemState(SetupItem::UiAutostart);
    const SetupItemState service = controller->itemState(SetupItem::Service);
    if (autostart == SetupItemState::Failed
        || service == SetupItemState::Failed) {
        return SetupItemState::Failed;
    }
    if (autostart == SetupItemState::Running
        || service == SetupItemState::Running) {
        return SetupItemState::Running;
    }
    if (autostart == SetupItemState::Succeeded
        && service == SetupItemState::Succeeded) {
        return SetupItemState::Succeeded;
    }
    return SetupItemState::Pending;
}

void setError(QLabel *label, const QString &error)
{
    label->setText(error);
    label->setVisible(!error.isEmpty());
}

bool hasFailure(OnboardingSetupController *controller)
{
    if (!controller->aggregateError().isEmpty()) {
        return true;
    }
    for (SetupItem item : {SetupItem::Model, SetupItem::UiAutostart,
                           SetupItem::Service, SetupItem::Fcitx}) {
        if (controller->itemState(item) == SetupItemState::Failed) {
            return true;
        }
    }
    return false;
}

} // namespace

OnboardingDialog::OnboardingDialog(OnboardingSetupController *controller,
                                   QWidget *parent)
    : Dtk::Widget::DDialog(parent)
    , controller_(controller)
    , pages_(new QStackedWidget)
    , backButton_(new QPushButton(QStringLiteral("上一步")))
    , nextButton_(new QPushButton(QStringLiteral("下一步")))
    , pageIndicator_(new QLabel)
    , modelStatusLabel_(nullptr)
    , modelErrorLabel_(nullptr)
    , serviceStatusLabel_(nullptr)
    , serviceErrorLabel_(nullptr)
    , fcitxStatusLabel_(nullptr)
    , fcitxErrorLabel_(nullptr)
    , aggregateErrorLabel_(nullptr)
    , modelProgressBar_(nullptr)
    , modelProgressLabel_(nullptr)
{
    Q_ASSERT(controller_);
    setObjectName(QStringLiteral("onboardingDialog"));
    setTitle(QStringLiteral("欢迎使用 EchoFlow"));
    setWordWrapTitle(true);
    setWindowIcon(QApplication::windowIcon());
    setIcon(QApplication::windowIcon());
    setModal(false);
    setMinimumSize(540, 450);
    setOnButtonClickedClose(false);

    auto *content = new QWidget(this);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(20, 8, 20, 12);
    layout->setSpacing(16);

    pages_->setObjectName(QStringLiteral("pages"));
    pages_->addWidget(createIntroPage());
    pages_->addWidget(createShortcutPage());
    pages_->addWidget(createSettingsPage());
    pages_->addWidget(createSetupPage());
    layout->addWidget(pages_, 1);

    auto *navigation = new QHBoxLayout;
    backButton_->setObjectName(QStringLiteral("backButton"));
    nextButton_->setObjectName(QStringLiteral("nextButton"));
    pageIndicator_->setObjectName(QStringLiteral("pageIndicator"));
    backButton_->setAccessibleName(QStringLiteral("上一个引导页面"));
    nextButton_->setAccessibleName(QStringLiteral("下一个引导页面"));
    pageIndicator_->setAccessibleName(QStringLiteral("引导页面进度"));
    backButton_->setMinimumHeight(36);
    nextButton_->setMinimumHeight(36);
    pageIndicator_->setAlignment(Qt::AlignCenter);
    navigation->addWidget(backButton_);
    navigation->addStretch();
    navigation->addWidget(pageIndicator_);
    navigation->addStretch();
    navigation->addWidget(nextButton_);
    layout->addLayout(navigation);
    addContent(content);

    connect(backButton_, &QPushButton::clicked, this,
            [this] { setCurrentPage(currentPage() - 1); });
    connect(nextButton_, &QPushButton::clicked, this,
            &OnboardingDialog::handlePrimaryAction);
    connect(controller_, &OnboardingSetupController::itemStateChanged, this,
            [this] { renderSetup(); });
    connect(controller_, &OnboardingSetupController::progressChanged, this,
            &OnboardingDialog::renderProgress);
    connect(controller_, &OnboardingSetupController::runningChanged, this,
            [this] { renderSetup(); });
    connect(controller_, &OnboardingSetupController::aggregateErrorChanged,
            this, [this] { renderSetup(); });
    connect(controller_, &OnboardingSetupController::setupComplete, this,
            [this] { renderSetup(); });
    connect(controller_, &OnboardingSetupController::setupFailed, this,
            [this] { renderSetup(); });

    setCurrentPage(0);
    renderSetup();
}

int OnboardingDialog::currentPage() const
{
    return pages_->currentIndex();
}

void OnboardingDialog::showForIncompleteSetup()
{
    const bool resumeSetup = controller_->hasStarted()
        || controller_->isRunning() || hasFailure(controller_);
    setCurrentPage(resumeSetup ? 3 : 0);
    renderSetup();
    show();
    raise();
    activateWindow();
}

void OnboardingDialog::showForReplay()
{
    setCurrentPage(0);
    renderSetup();
    show();
    raise();
    activateWindow();
}

QWidget *OnboardingDialog::createIntroPage()
{
    return informationPage(
        QStringLiteral("离线、安全、流畅"), QStringLiteral("introHeading"),
        QStringLiteral("EchoFlow 的语音识别完全在本机运行，无需联网，录音不会离开你的设备。"),
        QStringLiteral("introDescriptionLabel"),
        {{QStringLiteral("离线使用，保护隐私。"),
          QStringLiteral("introPrivacyLabel")},
         {QStringLiteral("说完即可快速输入到当前文本框。"),
          QStringLiteral("introResponsiveLabel")}});
}

QWidget *OnboardingDialog::createShortcutPage()
{
    return informationPage(
        QStringLiteral("按右 Ctrl 键开始说话"),
        QStringLiteral("shortcutHeading"),
        QStringLiteral("第一次按下右 Ctrl 键开始录音，第二次按下停止录音。"),
        QStringLiteral("shortcutDescriptionLabel"),
        {{QStringLiteral("识别完成后，文字会自动输入到当前聚焦的文本框。"),
          QStringLiteral("shortcutTranscriptLabel")}});
}

QWidget *OnboardingDialog::createSettingsPage()
{
    return informationPage(
        QStringLiteral("从托盘打开设置"), QStringLiteral("settingsHeading"),
        QStringLiteral("通过系统托盘可以随时打开 EchoFlow 设置。"),
        QStringLiteral("settingsDescriptionLabel"),
        {{QStringLiteral("在设置中选择模型、语言、麦克风和下载镜像。"),
          QStringLiteral("settingsOptionsLabel")},
         {QStringLiteral("你也可以随时从托盘重播本使用指南。"),
          QStringLiteral("settingsReplayLabel")}});
}

QWidget *OnboardingDialog::createSetupPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(10);
    layout->addWidget(pageTitle(QStringLiteral("准备开始使用"), page,
                                QStringLiteral("setupHeading")));
    layout->addWidget(wrappedLabel(
        QStringLiteral("下载语音模型，并检查后台服务和 Fcitx 是否就绪。"),
        page, QStringLiteral("setupDescriptionLabel")));

    auto addRow = [layout, page](const QString &name, const QString &statusName,
                                 const QString &errorName, QLabel **status,
                                 QLabel **error) {
        auto *frame = new QFrame(page);
        frame->setFrameShape(QFrame::StyledPanel);
        auto *row = new QVBoxLayout(frame);
        row->setContentsMargins(12, 8, 12, 8);
        row->setSpacing(4);
        auto *heading = new QHBoxLayout;
        auto *nameLabel = new QLabel(name, frame);
        QFont font = nameLabel->font();
        font.setBold(true);
        nameLabel->setFont(font);
        *status = new QLabel(frame);
        (*status)->setObjectName(statusName);
        (*status)->setAccessibleName(name + QStringLiteral("状态"));
        heading->addWidget(nameLabel);
        heading->addStretch();
        heading->addWidget(*status);
        row->addLayout(heading);
        *error = wrappedLabel({}, frame);
        (*error)->setObjectName(errorName);
        (*error)->setAccessibleName(name + QStringLiteral("错误"));
        (*error)->hide();
        row->addWidget(*error);
        layout->addWidget(frame);
        return row;
    };

    QVBoxLayout *modelRow = addRow(
        QStringLiteral("语音模型"), QStringLiteral("modelStatusLabel"),
        QStringLiteral("modelErrorLabel"), &modelStatusLabel_,
        &modelErrorLabel_);
    modelProgressBar_ = new QProgressBar(page);
    modelProgressBar_->setObjectName(QStringLiteral("modelProgressBar"));
    modelProgressBar_->setAccessibleName(QStringLiteral("语音模型下载进度"));
    modelProgressBar_->setTextVisible(false);
    modelProgressBar_->hide();
    modelProgressLabel_ = wrappedLabel({}, page);
    modelProgressLabel_->setObjectName(QStringLiteral("modelProgressLabel"));
    modelProgressLabel_->hide();
    modelRow->insertWidget(1, modelProgressBar_);
    modelRow->insertWidget(2, modelProgressLabel_);

    addRow(QStringLiteral("后台服务"),
           QStringLiteral("serviceStatusLabel"),
           QStringLiteral("serviceErrorLabel"), &serviceStatusLabel_,
           &serviceErrorLabel_);
    addRow(QStringLiteral("Fcitx 就绪状态"),
           QStringLiteral("fcitxStatusLabel"),
           QStringLiteral("fcitxErrorLabel"), &fcitxStatusLabel_,
           &fcitxErrorLabel_);

    aggregateErrorLabel_ = wrappedLabel({}, page);
    aggregateErrorLabel_->setObjectName(QStringLiteral("aggregateErrorLabel"));
    aggregateErrorLabel_->setAccessibleName(QStringLiteral("准备过程错误"));
    aggregateErrorLabel_->hide();
    layout->addWidget(aggregateErrorLabel_);
    layout->addStretch();
    return page;
}

void OnboardingDialog::setCurrentPage(int page)
{
    page = std::clamp(page, 0, pages_->count() - 1);
    pages_->setCurrentIndex(page);
    backButton_->setEnabled(page > 0);
    pageIndicator_->setText(
        QStringLiteral("第 %1 页，共 %2 页")
            .arg(page + 1)
            .arg(pages_->count()));
    renderSetup();
}

void OnboardingDialog::handlePrimaryAction()
{
    if (currentPage() < pages_->count() - 1) {
        setCurrentPage(currentPage() + 1);
        return;
    }
    if (controller_->isComplete()) {
        emit finishedAndSettingsRequested();
        return;
    }
    if (controller_->isRunning()) {
        return;
    }
    if (hasFailure(controller_)) {
        controller_->retryFailed();
    } else {
        controller_->start();
    }
    renderSetup();
}

void OnboardingDialog::renderSetup()
{
    modelStatusLabel_->setText(stateText(controller_->itemState(SetupItem::Model)));
    setError(modelErrorLabel_, controller_->itemError(SetupItem::Model));

    serviceStatusLabel_->setText(stateText(combinedServiceState(controller_)));
    QStringList serviceErrors;
    const QString autostartError =
        controller_->itemError(SetupItem::UiAutostart);
    const QString serviceError = controller_->itemError(SetupItem::Service);
    if (!autostartError.isEmpty()) {
        serviceErrors.append(QStringLiteral("托盘自启动：%1").arg(autostartError));
    }
    if (!serviceError.isEmpty()) {
        serviceErrors.append(QStringLiteral("后台服务：%1").arg(serviceError));
    }
    setError(serviceErrorLabel_, serviceErrors.join(QLatin1Char('\n')));

    fcitxStatusLabel_->setText(stateText(controller_->itemState(SetupItem::Fcitx)));
    setError(fcitxErrorLabel_, controller_->itemError(SetupItem::Fcitx));
    setError(aggregateErrorLabel_, controller_->aggregateError());

    const bool showProgress =
        controller_->itemState(SetupItem::Model) == SetupItemState::Running;
    modelProgressBar_->setVisible(showProgress);
    modelProgressLabel_->setVisible(showProgress);
    if (showProgress) {
        renderProgress(controller_->progressDone(), controller_->progressTotal());
    }

    if (currentPage() < pages_->count() - 1) {
        nextButton_->setText(QStringLiteral("下一步"));
        nextButton_->setEnabled(true);
        nextButton_->setAccessibleName(QStringLiteral("下一个引导页面"));
    } else if (controller_->isComplete()) {
        nextButton_->setText(QStringLiteral("完成并打开设置"));
        nextButton_->setEnabled(true);
        nextButton_->setAccessibleName(QStringLiteral("完成并打开设置"));
    } else if (controller_->isRunning()) {
        nextButton_->setText(QStringLiteral("准备中…"));
        nextButton_->setEnabled(false);
        nextButton_->setAccessibleName(QStringLiteral("正在准备 EchoFlow"));
    } else if (hasFailure(controller_)) {
        nextButton_->setText(QStringLiteral("重试"));
        nextButton_->setEnabled(true);
        nextButton_->setAccessibleName(QStringLiteral("重试失败的准备项目"));
    } else {
        nextButton_->setText(QStringLiteral("开始使用 EchoFlow"));
        nextButton_->setEnabled(true);
        nextButton_->setAccessibleName(QStringLiteral("开始准备 EchoFlow"));
    }
}

void OnboardingDialog::renderProgress(qint64 done, qint64 total)
{
    const double mebibyte = 1024.0 * 1024.0;
    if (total > 0) {
        modelProgressBar_->setRange(0, 1000);
        modelProgressBar_->setValue(static_cast<int>(
            std::clamp(done / static_cast<double>(total), 0.0, 1.0) * 1000));
        modelProgressLabel_->setText(
            QStringLiteral("已下载 %1 MB，共 %2 MB")
                .arg(QString::number(done / mebibyte, 'f', 1),
                     QString::number(total / mebibyte, 'f', 1)));
    } else {
        modelProgressBar_->setRange(0, 0);
        modelProgressLabel_->setText(
            done > 0
                ? QStringLiteral("已下载 %1 MB")
                      .arg(QString::number(done / mebibyte, 'f', 1))
                : QStringLiteral("正在等待下载进度…"));
    }
}
