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

QLabel *wrappedLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

QLabel *pageTitle(const QString &text, QWidget *parent)
{
    auto *label = wrappedLabel(text, parent);
    QFont font = label->font();
    font.setPointSize(font.pointSize() + 5);
    font.setBold(true);
    label->setFont(font);
    return label;
}

QWidget *informationPage(const QString &title, const QString &body,
                         const QStringList &points)
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(14);
    layout->addWidget(pageTitle(title, page));
    layout->addWidget(wrappedLabel(body, page));
    for (const QString &point : points) {
        auto *label = wrappedLabel(QStringLiteral("•  %1").arg(point), page);
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
        return QStringLiteral("Pending");
    case SetupItemState::Running:
        return QStringLiteral("Running");
    case SetupItemState::Succeeded:
        return QStringLiteral("Succeeded");
    case SetupItemState::Failed:
        return QStringLiteral("Failed");
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
    , backButton_(new QPushButton(QStringLiteral("Back")))
    , nextButton_(new QPushButton(QStringLiteral("Next")))
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
    setTitle(QStringLiteral("Welcome to EchoFlow"));
    setWordWrapTitle(true);
    setIcon(QIcon::fromTheme(QStringLiteral("echoflow"),
                             QApplication::windowIcon()));
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
    backButton_->setAccessibleName(QStringLiteral("Previous onboarding page"));
    nextButton_->setAccessibleName(QStringLiteral("Next onboarding page"));
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
        QStringLiteral("Private voice input, ready when you are"),
        QStringLiteral("EchoFlow turns speech into text locally on this "
                       "computer."),
        {QStringLiteral("Offline: recognition does not need a cloud service."),
         QStringLiteral("Private: recordings stay on your device."),
         QStringLiteral("Responsive: dictate directly into the focused field.")});
}

QWidget *OnboardingDialog::createShortcutPage()
{
    return informationPage(
        QStringLiteral("Press Right Ctrl to dictate"),
        QStringLiteral("The same shortcut starts and stops each recording."),
        {QStringLiteral("Press Right Ctrl once to start recording."),
         QStringLiteral("Press it again to stop."),
         QStringLiteral("Your transcript is inserted into the focused text field.")});
}

QWidget *OnboardingDialog::createSettingsPage()
{
    return informationPage(
        QStringLiteral("Tune EchoFlow from the tray"),
        QStringLiteral("Open EchoFlow settings at any time from the system tray."),
        {QStringLiteral("Choose the recognition model and language."),
         QStringLiteral("Select a microphone and download mirror."),
         QStringLiteral("Replay this usage guide whenever you need it.")});
}

QWidget *OnboardingDialog::createSetupPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(10);
    layout->addWidget(pageTitle(QStringLiteral("Prepare EchoFlow"), page));
    layout->addWidget(wrappedLabel(
        QStringLiteral("Download the model and check the background components "
                       "needed for voice input."),
        page));

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
        heading->addWidget(nameLabel);
        heading->addStretch();
        heading->addWidget(*status);
        row->addLayout(heading);
        *error = wrappedLabel({}, frame);
        (*error)->setObjectName(errorName);
        (*error)->setAccessibleName(name + QStringLiteral(" error"));
        (*error)->hide();
        row->addWidget(*error);
        layout->addWidget(frame);
        return row;
    };

    QVBoxLayout *modelRow = addRow(
        QStringLiteral("Model"), QStringLiteral("modelStatusLabel"),
        QStringLiteral("modelErrorLabel"), &modelStatusLabel_,
        &modelErrorLabel_);
    modelProgressBar_ = new QProgressBar(page);
    modelProgressBar_->setObjectName(QStringLiteral("modelProgressBar"));
    modelProgressBar_->setTextVisible(false);
    modelProgressBar_->hide();
    modelProgressLabel_ = wrappedLabel({}, page);
    modelProgressLabel_->setObjectName(QStringLiteral("modelProgressLabel"));
    modelProgressLabel_->hide();
    modelRow->insertWidget(1, modelProgressBar_);
    modelRow->insertWidget(2, modelProgressLabel_);

    addRow(QStringLiteral("Background service"),
           QStringLiteral("serviceStatusLabel"),
           QStringLiteral("serviceErrorLabel"), &serviceStatusLabel_,
           &serviceErrorLabel_);
    addRow(QStringLiteral("Fcitx readiness"),
           QStringLiteral("fcitxStatusLabel"),
           QStringLiteral("fcitxErrorLabel"), &fcitxStatusLabel_,
           &fcitxErrorLabel_);

    aggregateErrorLabel_ = wrappedLabel({}, page);
    aggregateErrorLabel_->setObjectName(QStringLiteral("aggregateErrorLabel"));
    aggregateErrorLabel_->setAccessibleName(QStringLiteral("Setup error"));
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
        QStringLiteral("%1 / %2").arg(page + 1).arg(pages_->count()));
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
        serviceErrors.append(QStringLiteral("Tray startup: %1").arg(autostartError));
    }
    if (!serviceError.isEmpty()) {
        serviceErrors.append(QStringLiteral("Service: %1").arg(serviceError));
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
        nextButton_->setText(QStringLiteral("Next"));
        nextButton_->setEnabled(true);
        nextButton_->setAccessibleName(QStringLiteral("Next onboarding page"));
    } else if (controller_->isComplete()) {
        nextButton_->setText(QStringLiteral("Finish"));
        nextButton_->setEnabled(true);
        nextButton_->setAccessibleName(QStringLiteral("Finish and open settings"));
    } else if (controller_->isRunning()) {
        nextButton_->setText(QStringLiteral("Preparing..."));
        nextButton_->setEnabled(false);
        nextButton_->setAccessibleName(QStringLiteral("Setup is running"));
    } else if (hasFailure(controller_)) {
        nextButton_->setText(QStringLiteral("Retry"));
        nextButton_->setEnabled(true);
        nextButton_->setAccessibleName(QStringLiteral("Retry failed setup items"));
    } else {
        nextButton_->setText(QStringLiteral("Start"));
        nextButton_->setEnabled(true);
        nextButton_->setAccessibleName(QStringLiteral("Start EchoFlow setup"));
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
            QStringLiteral("%1 MB of %2 MB")
                .arg(QString::number(done / mebibyte, 'f', 1),
                     QString::number(total / mebibyte, 'f', 1)));
    } else {
        modelProgressBar_->setRange(0, 0);
        modelProgressLabel_->setText(
            done > 0
                ? QStringLiteral("%1 MB downloaded")
                      .arg(QString::number(done / mebibyte, 'f', 1))
                : QStringLiteral("Waiting for download progress..."));
    }
}
