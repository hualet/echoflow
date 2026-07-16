// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "SetupCommandRunner.h"

#include <QObject>
#include <QString>

#include <array>

class OnboardingState;

enum class SetupItem { Model, UiAutostart, Service, Fcitx };
enum class SetupItemState { Pending, Running, Succeeded, Failed };

class ModelSetupSource : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    virtual bool modelPresent() const = 0;
    virtual bool downloadRunning() const = 0;
    virtual void startDownload() = 0;

signals:
    void progress(qint64 done, qint64 total);
    void finished(bool ok, const QString &error);
};

class OnboardingSetupController final : public QObject {
    Q_OBJECT
public:
    OnboardingSetupController(ModelSetupSource *model,
                              SetupCommandRunner *runner,
                              OnboardingState *state,
                              QObject *parent = nullptr);

    SetupItemState itemState(SetupItem item) const;
    QString itemError(SetupItem item) const;
    qint64 progressDone() const;
    qint64 progressTotal() const;
    bool isRunning() const;
    bool isComplete() const;
    bool hasStarted() const;
    QString aggregateError() const;

public slots:
    void start();
    void retryFailed();

signals:
    void itemStateChanged(SetupItem item, SetupItemState state,
                          const QString &error);
    void progressChanged(qint64 done, qint64 total);
    void runningChanged(bool running);
    void aggregateErrorChanged(const QString &error);
    void setupComplete();
    void setupFailed(const QString &error);

private:
    static std::size_t indexOf(SetupItem item);
    void startItem(SetupItem item);
    void setItemState(SetupItem item, SetupItemState state,
                      const QString &error = {});
    void handleCommandFinished(const QString &id, bool ok,
                               const QString &error);
    void checkForCompletion();
    bool allItemsSucceeded() const;

    ModelSetupSource *model_;
    SetupCommandRunner *runner_;
    OnboardingState *state_;
    std::array<SetupItemState, 4> itemStates_;
    std::array<QString, 4> itemErrors_;
    qint64 progressDone_ = 0;
    qint64 progressTotal_ = 0;
    bool complete_ = false;
    bool started_ = false;
    QString aggregateError_;
};

Q_DECLARE_METATYPE(SetupItem)
Q_DECLARE_METATYPE(SetupItemState)
