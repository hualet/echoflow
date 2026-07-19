// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <DDialog>

class QLabel;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QString;
class QWidget;
class OnboardingSetupController;

class OnboardingDialog final : public Dtk::Widget::DDialog {
    Q_OBJECT
public:
    explicit OnboardingDialog(OnboardingSetupController *controller,
                              QWidget *parent = nullptr);

    int currentPage() const;

public slots:
    void showForIncompleteSetup();
    void showForReplay();

signals:
    void finishedAndSettingsRequested();

private:
    QWidget *createIntroPage();
    QWidget *createShortcutPage();
    QWidget *createSettingsPage();
    QWidget *createVisualPage(const QString &illustrationPath,
                              const QString &illustrationObjectName,
                              const QString &illustrationAccessibleName,
                              const QString &heading,
                              const QString &headingObjectName,
                              const QString &description,
                              const QString &descriptionObjectName,
                              const QString &tag = {},
                              const QString &tagObjectName = {});
    QLabel *createIllustration(const QString &resourcePath,
                               const QString &objectName,
                               const QString &accessibleName,
                               QWidget *parent);
    QWidget *createSetupPage();
    void setCurrentPage(int page);
    void handlePrimaryAction();
    void renderSetup();
    void renderProgress(qint64 done, qint64 total);

    OnboardingSetupController *controller_;
    QStackedWidget *pages_;
    QPushButton *backButton_;
    QPushButton *nextButton_;
    QWidget *pageDotsWidget_;
    QLabel *modelStatusLabel_;
    QLabel *modelErrorLabel_;
    QLabel *serviceStatusLabel_;
    QLabel *serviceErrorLabel_;
    QLabel *fcitxStatusLabel_;
    QLabel *fcitxErrorLabel_;
    QLabel *aggregateErrorLabel_;
    QProgressBar *modelProgressBar_;
    QLabel *modelProgressLabel_;
};
