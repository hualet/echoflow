// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "OnboardingDialog.h"

#include "OnboardingIllustration.h"
#include "OnboardingSetupController.h"

#include <DBackgroundGroup>
#include <DTitlebar>

#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int kSemanticTagCornerRadius = 10;
constexpr int kPageDotDiameter = 8;
constexpr int kPageDotSpacing = 6;

class PageDotsIndicator final : public QWidget {
public:
    explicit PageDotsIndicator(int pageCount, QWidget *parent = nullptr)
        : QWidget(parent)
        , pageCount_(pageCount)
    {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setAccessibleName(progressText());
        setProperty("activePage", currentPage_);
    }

    QSize sizeHint() const override
    {
        return QSize(pageCount_ * kPageDotDiameter
                         + (pageCount_ - 1) * kPageDotSpacing,
                     kPageDotDiameter);
    }

    void setCurrentPage(int page)
    {
        currentPage_ = std::clamp(page, 0, pageCount_ - 1);
        setProperty("activePage", currentPage_);
        setAccessibleName(progressText());
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        const int contentWidth = sizeHint().width();
        int x = (width() - contentWidth) / 2;
        const int y = (height() - kPageDotDiameter) / 2;
        for (int page = 0; page < pageCount_; ++page) {
            painter.setBrush(palette().brush(
                page == currentPage_ ? QPalette::Highlight : QPalette::Mid));
            painter.drawEllipse(QRect(x, y, kPageDotDiameter,
                                      kPageDotDiameter));
            x += kPageDotDiameter + kPageDotSpacing;
        }
    }

    void changeEvent(QEvent *event) override
    {
        if (event->type() == QEvent::PaletteChange) {
            update();
        }
        QWidget::changeEvent(event);
    }

private:
    QString progressText() const
    {
        return QStringLiteral("第 %1 页，共 %2 页")
            .arg(currentPage_ + 1)
            .arg(pageCount_);
    }

    int pageCount_;
    int currentPage_ = 0;
};

class SemanticTagLabel final : public QLabel {
public:
    using QLabel::QLabel;

protected:
    void paintEvent(QPaintEvent *event) override
    {
        {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen);
            painter.setBrush(palette().brush(backgroundRole()));
            painter.drawRoundedRect(rect(), kSemanticTagCornerRadius,
                                    kSemanticTagCornerRadius);
        }
        QLabel::paintEvent(event);
    }
};

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
    , pageDotsWidget_(new PageDotsIndicator(4))
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
    setWindowIcon(QApplication::windowIcon());
    setTitle({});
    setIcon({});
    setModal(false);
    setMinimumSize(680, 500);
    setOnButtonClickedClose(false);

    auto *titlebar = findChild<Dtk::Widget::DTitlebar *>(
        QString(), Qt::FindDirectChildrenOnly);
    Q_ASSERT(titlebar);
    titlebar->setObjectName(QStringLiteral("onboardingTitlebar"));
    titlebar->setBackgroundTransparent(true);
    titlebar->setSeparatorVisible(false);

    auto *titleWidget = new QWidget(titlebar);
    auto *titleLayout = new QHBoxLayout(titleWidget);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(8);

    auto *titleIcon = new QLabel(titleWidget);
    titleIcon->setObjectName(QStringLiteral("onboardingTitleIcon"));
    titleIcon->setAccessibleName({});
    titleIcon->setPixmap(QApplication::windowIcon().pixmap(20, 20));
    titleIcon->setFixedSize(20, 20);

    auto *titleLabel =
        new QLabel(QStringLiteral("欢迎使用 EchoFlow"), titleWidget);
    titleLabel->setObjectName(QStringLiteral("onboardingTitleLabel"));
    titleLabel->setAccessibleName(titleLabel->text());
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);

    titleLayout->addWidget(titleIcon);
    titleLayout->addWidget(titleLabel);
    titlebar->setCustomWidget(titleWidget);

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

    auto *navigation = new QGridLayout;
    backButton_->setObjectName(QStringLiteral("backButton"));
    nextButton_->setObjectName(QStringLiteral("nextButton"));
    pageDotsWidget_->setObjectName(QStringLiteral("pageDots"));
    backButton_->setAccessibleName(QStringLiteral("上一个引导页面"));
    nextButton_->setAccessibleName(QStringLiteral("下一个引导页面"));
    backButton_->setMinimumHeight(36);
    nextButton_->setMinimumHeight(36);
    int navigationSideWidth = backButton_->sizeHint().width();
    const QString initialPrimaryText = nextButton_->text();
    for (const QString &text : {
             QStringLiteral("下一步"),
             QStringLiteral("开始使用 EchoFlow"),
             QStringLiteral("完成并打开设置"), QStringLiteral("准备中…"),
             QStringLiteral("重试")}) {
        nextButton_->setText(text);
        navigationSideWidth =
            std::max(navigationSideWidth, nextButton_->sizeHint().width());
    }
    nextButton_->setText(initialPrimaryText);
    navigation->setColumnMinimumWidth(0, navigationSideWidth);
    navigation->setColumnMinimumWidth(2, navigationSideWidth);
    navigation->setColumnStretch(0, 1);
    navigation->setColumnStretch(2, 1);
    navigation->addWidget(backButton_, 0, 0, Qt::AlignLeft);
    navigation->addWidget(pageDotsWidget_, 0, 1, Qt::AlignCenter);
    navigation->addWidget(nextButton_, 0, 2, Qt::AlignRight);
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
    const bool resumeSetup =
        controller_->hasStarted() || hasFailure(controller_);
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
    return createVisualPage(
        QStringLiteral(":/onboarding/intro.png"),
        QStringLiteral("introIllustration"),
        QStringLiteral("本机离线语音识别示意图"),
        QStringLiteral("说话，就能输入"), QStringLiteral("introHeading"),
        QStringLiteral("语音识别在本机完成，录音不会离开你的设备。"),
        QStringLiteral("introDescriptionLabel"),
        QStringLiteral("离线 · 隐私 · 快速"),
        QStringLiteral("introTagLabel"));
}

QWidget *OnboardingDialog::createShortcutPage()
{
    return createVisualPage(
        QStringLiteral(":/onboarding/shortcut.png"),
        QStringLiteral("shortcutIllustration"),
        QStringLiteral("右 Ctrl 语音输入快捷键示意图"),
        QStringLiteral("按右 Ctrl，开始说话"),
        QStringLiteral("shortcutHeading"),
        QStringLiteral("再按一次结束，识别文字会直接进入当前输入框。"),
        QStringLiteral("shortcutDescriptionLabel"),
        QStringLiteral("右 Ctrl · 开始 / 结束"),
        QStringLiteral("shortcutTagLabel"));
}

QWidget *OnboardingDialog::createSettingsPage()
{
    return createVisualPage(
        QStringLiteral(":/onboarding/settings.png"),
        QStringLiteral("settingsIllustration"),
        QStringLiteral("托盘与设置入口示意图"),
        QStringLiteral("需要调整？都在托盘里"),
        QStringLiteral("settingsHeading"),
        QStringLiteral("切换模型、语言、麦克风，也可以随时重播这份指引。"),
        QStringLiteral("settingsDescriptionLabel"),
        QStringLiteral("托盘 · 设置 · 使用指引"),
        QStringLiteral("settingsTagLabel"));
}

QWidget *OnboardingDialog::createVisualPage(
    const QString &illustrationPath, const QString &illustrationObjectName,
    const QString &illustrationAccessibleName, const QString &heading,
    const QString &headingObjectName, const QString &description,
    const QString &descriptionObjectName, const QString &tag,
    const QString &tagObjectName)
{
    auto *page = new QWidget;
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(24);
    layout->addWidget(createIllustration(
                          illustrationPath, illustrationObjectName,
                          illustrationAccessibleName, page),
                      45);

    auto *copy = new QVBoxLayout;
    copy->setSpacing(14);
    copy->addStretch();
    copy->addWidget(pageTitle(heading, page, headingObjectName));
    copy->addWidget(wrappedLabel(description, page, descriptionObjectName));
    if (!tag.isEmpty()) {
        auto *tagLabel = new SemanticTagLabel(tag, page);
        tagLabel->setObjectName(tagObjectName);
        tagLabel->setWordWrap(false);
        tagLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        tagLabel->setProperty("semanticTag", true);
        tagLabel->setProperty("cornerRadius", kSemanticTagCornerRadius);
        tagLabel->setBackgroundRole(QPalette::AlternateBase);
        tagLabel->setForegroundRole(QPalette::Text);
        tagLabel->setContentsMargins(10, 4, 10, 4);
        const QMargins margins = tagLabel->contentsMargins();
        tagLabel->setMinimumWidth(
            tagLabel->fontMetrics().horizontalAdvance(tag)
            + margins.left() + margins.right());
        tagLabel->setMaximumHeight(tagLabel->sizeHint().height());
        tagLabel->setSizePolicy(QSizePolicy::Maximum,
                                QSizePolicy::Preferred);
        copy->addWidget(tagLabel, 0, Qt::AlignLeft);
    }
    copy->addStretch();
    layout->addLayout(copy, 55);
    return page;
}

QLabel *OnboardingDialog::createIllustration(
    const QString &resourcePath, const QString &objectName,
    const QString &accessibleName, QWidget *parent)
{
    auto *illustration = new OnboardingIllustration(
        resourcePath, accessibleName, parent);
    illustration->setObjectName(objectName);
    return illustration;
}

QWidget *OnboardingDialog::createSetupPage()
{
    auto *page = new QWidget;
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(20);

    auto *illustration = createIllustration(
        QStringLiteral(":/onboarding/setup.png"),
        QStringLiteral("setupIllustration"),
        QStringLiteral("模型下载与服务准备示意图"), page);
    illustration->setMaximumSize(210, 260);
    illustration->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    layout->addWidget(illustration, 34, Qt::AlignCenter);

    auto *scroll = new QScrollArea(page);
    scroll->setObjectName(QStringLiteral("setupScrollArea"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *setupContent = new QWidget(scroll);
    auto *setup = new QVBoxLayout(setupContent);
    setup->setSizeConstraint(QLayout::SetMinAndMaxSize);
    setup->setSpacing(6);
    setup->addWidget(pageTitle(QStringLiteral("准备好，就可以开始"),
                               setupContent,
                               QStringLiteral("setupHeading")));
    setup->addWidget(wrappedLabel(
        QStringLiteral("首次下载模型需要联网；服务和 Fcitx 会同步检查。"),
        setupContent, QStringLiteral("setupDescriptionLabel")));

    auto *groupLayout = new QVBoxLayout;
    groupLayout->setContentsMargins(0, 0, 0, 0);
    groupLayout->setSpacing(0);
    auto *statusGroup = new Dtk::Widget::DBackgroundGroup(
        groupLayout, setupContent);
    statusGroup->setObjectName(QStringLiteral("setupStatusGroup"));
    statusGroup->setBackgroundRole(QPalette::Base);
    statusGroup->setItemMargins(QMargins(12, 8, 12, 8));
    statusGroup->setItemSpacing(0);
    statusGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    auto addRow = [groupLayout, statusGroup](
                      const QString &rowName, const QString &name,
                      const QString &statusName, const QString &errorName,
                      QLabel **status, QLabel **error) {
        auto *rowWidget = new QWidget(statusGroup);
        rowWidget->setObjectName(rowName);
        auto *row = new QVBoxLayout(rowWidget);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);
        auto *heading = new QHBoxLayout;
        auto *nameLabel = new QLabel(name, rowWidget);
        QFont font = nameLabel->font();
        font.setBold(true);
        nameLabel->setFont(font);
        *status = new QLabel(rowWidget);
        (*status)->setObjectName(statusName);
        (*status)->setAccessibleName(name + QStringLiteral("状态"));
        heading->addWidget(nameLabel);
        heading->addStretch();
        heading->addWidget(*status);
        row->addLayout(heading);
        *error = wrappedLabel({}, rowWidget);
        (*error)->setObjectName(errorName);
        (*error)->setAccessibleName(name + QStringLiteral("错误"));
        (*error)->hide();
        row->addWidget(*error);
        groupLayout->addWidget(rowWidget);
        return row;
    };

    QVBoxLayout *modelRow = addRow(
        QStringLiteral("modelSetupRow"), QStringLiteral("语音模型"),
        QStringLiteral("modelStatusLabel"), QStringLiteral("modelErrorLabel"),
        &modelStatusLabel_, &modelErrorLabel_);
    modelProgressBar_ = new QProgressBar(setupContent);
    modelProgressBar_->setObjectName(QStringLiteral("modelProgressBar"));
    modelProgressBar_->setAccessibleName(QStringLiteral("语音模型下载进度"));
    modelProgressBar_->setTextVisible(false);
    modelProgressBar_->hide();
    modelProgressLabel_ = wrappedLabel({}, setupContent);
    modelProgressLabel_->setObjectName(QStringLiteral("modelProgressLabel"));
    modelProgressLabel_->hide();
    modelRow->insertWidget(1, modelProgressBar_);
    modelRow->insertWidget(2, modelProgressLabel_);

    addRow(QStringLiteral("serviceSetupRow"), QStringLiteral("后台服务"),
           QStringLiteral("serviceStatusLabel"),
           QStringLiteral("serviceErrorLabel"), &serviceStatusLabel_,
           &serviceErrorLabel_);
    addRow(QStringLiteral("fcitxSetupRow"),
           QStringLiteral("Fcitx 就绪状态"),
           QStringLiteral("fcitxStatusLabel"),
           QStringLiteral("fcitxErrorLabel"), &fcitxStatusLabel_,
           &fcitxErrorLabel_);
    setup->addWidget(statusGroup);

    aggregateErrorLabel_ = wrappedLabel({}, setupContent);
    aggregateErrorLabel_->setObjectName(QStringLiteral("aggregateErrorLabel"));
    aggregateErrorLabel_->setAccessibleName(QStringLiteral("准备过程错误"));
    aggregateErrorLabel_->hide();
    setup->addWidget(aggregateErrorLabel_);
    setup->addStretch();
    scroll->setWidget(setupContent);
    layout->addWidget(scroll, 66);
    return page;
}

void OnboardingDialog::setCurrentPage(int page)
{
    page = std::clamp(page, 0, pages_->count() - 1);
    pages_->setCurrentIndex(page);
    backButton_->setEnabled(page > 0);
    static_cast<PageDotsIndicator *>(pageDotsWidget_)->setCurrentPage(page);
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
