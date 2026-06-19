// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ModelDownloadCoordinator.h"

#include "ModelDownloader.h"

namespace echoflow {

ModelDownloadCoordinator::ModelDownloadCoordinator(QObject* parent)
    : QObject(parent) {}

ModelDownloadCoordinator* ModelDownloadCoordinator::instance() {
    static ModelDownloadCoordinator coordinator;
    return &coordinator;
}

void ModelDownloadCoordinator::start(const ModelEntry& entry,
                                     const QString& dir,
                                     const QString& baseUrl) {
    const QString id = QString::fromStdString(entry.id);
    if (active_.contains(id)) {
        return;  // already running — ignore double-start
    }

    // Parented to `this` (the tray-host-lifetime singleton), not to any widget.
    auto* d = new ModelDownloader(entry, dir, baseUrl, this);
    active_.insert(id, d);
    cache_[id] = DownloadSnapshot{DownloadState::Downloading, 0, 0, QString(), QString()};
    emit stateChanged(id, DownloadState::Downloading, QString());

    // Each downloader's signals are connected with a lambda capturing its id,
    // so the coordinator can fan out one set of signals to N widgets without
    // sender() lookup.
    connect(d, &ModelDownloader::progress, this,
            [this, id](qint64 done, qint64 total, const QString& file) {
                cache_[id].done = done;
                cache_[id].total = total;
                cache_[id].currentFile = file;
                emit progress(id, done, total, file);
            });
    connect(d, &ModelDownloader::finished, this,
            [this, id](bool ok, const QString& error) {
                cache_[id].state = ok ? DownloadState::Succeeded : DownloadState::Failed;
                cache_[id].error = ok ? QString() : error;
                emit stateChanged(id, cache_[id].state, error);
                // Terminal: drop ownership. The snapshot is retained so a
                // widget opened right after sees the result until the next
                // start() overwrites it.
                auto it = active_.find(id);
                if (it != active_.end()) {
                    it.value()->deleteLater();
                    active_.erase(it);
                }
            });

    d->start();
}

void ModelDownloadCoordinator::cancel(const QString& id) {
    auto it = active_.find(id);
    if (it != active_.end()) {
        // cancel() emits finished(false, "已取消") synchronously; the finished
        // lambda above handles the cache/signal/cleanup and erases from
        // active_, invalidating `it`. Do not reuse `it` below.
        it.value()->cancel();
    }
}

DownloadSnapshot ModelDownloadCoordinator::snapshot(const QString& id) const {
    return cache_.value(id);
}

}  // namespace echoflow
