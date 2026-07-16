// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ModelSetupAdapter.h"

#include <QDir>

#include <filesystem>
#include <stdexcept>
#include <utility>

namespace {

constexpr auto kDefaultModelId = "qwen3-asr-0.6b";

const echoflow::ModelEntry &defaultModel()
{
    return *echoflow::findModel(kDefaultModelId);
}

QString defaultModelId()
{
    return QString::fromLatin1(kDefaultModelId);
}

}  // namespace

ModelSetupAdapter::ModelSetupAdapter(QString configDir,
                                     MirrorProvider mirrorProvider,
                                     SnapshotProvider snapshotProvider,
                                     StartDownload startDownload,
                                     QObject *parent)
    : ModelSetupSource(parent),
      configDir_(std::move(configDir)),
      mirrorProvider_(std::move(mirrorProvider)),
      snapshotProvider_(std::move(snapshotProvider)),
      startDownload_(std::move(startDownload))
{
    if (!mirrorProvider_) {
        throw std::invalid_argument("mirror provider is required");
    }
    if (!snapshotProvider_) {
        throw std::invalid_argument("snapshot provider is required");
    }
    if (!startDownload_) {
        throw std::invalid_argument("start download callback is required");
    }
}

ModelSetupAdapter::ModelSetupAdapter(QString configDir, QString mirror,
                                     SnapshotProvider snapshotProvider,
                                     StartDownload startDownload,
                                     QObject *parent)
    : ModelSetupAdapter(
          std::move(configDir),
          [mirror = std::move(mirror)] { return mirror; },
          std::move(snapshotProvider), std::move(startDownload), parent)
{
}

ModelSetupAdapter::ModelSetupAdapter(QString configDir,
                                     MirrorProvider mirrorProvider,
                                     QObject *parent)
    : ModelSetupAdapter(
          std::move(configDir), std::move(mirrorProvider),
          [coordinator = QPointer<echoflow::ModelDownloadCoordinator>(
               echoflow::ModelDownloadCoordinator::instance())](const QString &id) {
              return coordinator ? coordinator->snapshot(id)
                                 : echoflow::DownloadSnapshot{};
          },
          [coordinator = QPointer<echoflow::ModelDownloadCoordinator>(
               echoflow::ModelDownloadCoordinator::instance())](
              const echoflow::ModelEntry &entry, const QString &dir,
              const QString &baseUrl) {
              if (coordinator) {
                  coordinator->start(entry, dir, baseUrl);
              }
          },
          parent)
{
    observeCoordinator(echoflow::ModelDownloadCoordinator::instance());
}

ModelSetupAdapter::ModelSetupAdapter(QString configDir, QString mirror,
                                     QObject *parent)
    : ModelSetupAdapter(std::move(configDir),
                        [mirror = std::move(mirror)] { return mirror; }, parent)
{
}

bool ModelSetupAdapter::modelPresent() const
{
    const QString dir = QDir(configDir_).filePath(defaultModelId());
    return echoflow::isModelPresent(std::filesystem::path(dir.toStdString()),
                                    defaultModel());
}

bool ModelSetupAdapter::downloadRunning() const
{
    return snapshotProvider_(defaultModelId()).state ==
           echoflow::DownloadState::Downloading;
}

void ModelSetupAdapter::startDownload()
{
    const QString dir = QDir(configDir_).filePath(defaultModelId());
    const QString baseUrl = mirrorProvider_() == QStringLiteral("official")
                                ? QStringLiteral("https://huggingface.co")
                                : QStringLiteral("https://hf-mirror.com");
    startDownload_(defaultModel(), dir, baseUrl);
}

void ModelSetupAdapter::observeCoordinator(
    echoflow::ModelDownloadCoordinator *coordinator)
{
    if (coordinator_ == coordinator) {
        return;
    }
    if (coordinator_) {
        disconnect(coordinator_, nullptr, this, nullptr);
    }
    coordinator_ = coordinator;
    if (!coordinator_) {
        return;
    }
    connect(coordinator_, &echoflow::ModelDownloadCoordinator::progress,
            this, &ModelSetupAdapter::handleProgress);
    connect(coordinator_, &echoflow::ModelDownloadCoordinator::stateChanged,
            this, &ModelSetupAdapter::handleStateChanged);
}

void ModelSetupAdapter::handleProgress(const QString &id, qint64 done,
                                       qint64 total, const QString &file)
{
    Q_UNUSED(file)
    if (id == defaultModelId()) {
        emit progress(done, total);
    }
}

void ModelSetupAdapter::handleStateChanged(const QString &id,
                                           echoflow::DownloadState state,
                                           const QString &error)
{
    if (id != defaultModelId()) {
        return;
    }
    if (state == echoflow::DownloadState::Succeeded) {
        emit finished(true, {});
    } else if (state == echoflow::DownloadState::Failed) {
        emit finished(false, error);
    }
}
