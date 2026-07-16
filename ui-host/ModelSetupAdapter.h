// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ModelDownloadCoordinator.h"
#include "OnboardingSetupController.h"

#include <QPointer>

#include <functional>

class ModelSetupAdapter final : public ModelSetupSource {
    Q_OBJECT
public:
    using MirrorProvider = std::function<QString()>;
    using SnapshotProvider =
        std::function<echoflow::DownloadSnapshot(const QString &)>;
    using StartDownload =
        std::function<void(const echoflow::ModelEntry &, const QString &,
                           const QString &)>;

    ModelSetupAdapter(QString configDir, MirrorProvider mirrorProvider,
                      SnapshotProvider snapshotProvider,
                      StartDownload startDownload,
                      QObject *parent = nullptr);
    ModelSetupAdapter(QString configDir, QString mirror,
                      SnapshotProvider snapshotProvider,
                      StartDownload startDownload,
                      QObject *parent = nullptr);
    ModelSetupAdapter(QString configDir, MirrorProvider mirrorProvider,
                      QObject *parent = nullptr);
    ModelSetupAdapter(QString configDir, QString mirror,
                      QObject *parent = nullptr);

    bool modelPresent() const override;
    bool downloadRunning() const override;
    void startDownload() override;

    void observeCoordinator(echoflow::ModelDownloadCoordinator *coordinator);

private slots:
    void handleProgress(const QString &id, qint64 done, qint64 total,
                        const QString &file);
    void handleStateChanged(const QString &id, echoflow::DownloadState state,
                            const QString &error);

private:
    QString configDir_;
    MirrorProvider mirrorProvider_;
    SnapshotProvider snapshotProvider_;
    StartDownload startDownload_;
    QPointer<echoflow::ModelDownloadCoordinator> coordinator_;
};
