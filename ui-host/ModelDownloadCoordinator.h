// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_MODEL_DOWNLOAD_COORDINATOR_H
#define ECHOFLOW_MODEL_DOWNLOAD_COORDINATOR_H

#include "ModelCatalog.h"

#include <QObject>
#include <QString>
#include <QHash>

namespace echoflow {

class ModelDownloader;

enum class DownloadState { Idle, Downloading, Succeeded, Failed };

// Cached view of one model's download, sufficient for a freshly-constructed
// ModelRowWidget to paint immediately without having seen prior progress
// signals (which it missed, having just been created).
struct DownloadSnapshot {
    DownloadState state = DownloadState::Idle;
    qint64 done = 0;
    qint64 total = 0;       // 0 == indeterminate (no Content-Length)
    QString currentFile;
    QString error;          // set on Failed; "已取消" for a user cancel
};

// Owns the active ModelDownloader instances for the lifetime of the tray host,
// so a download outlives the settings dialog being closed and reopened. The
// row widget never touches a ModelDownloader directly: it reads snapshot() and
// connects to progress()/stateChanged(). Reparenting downloaders to this
// singleton (new ModelDownloader(..., this)) is what detaches their lifetime
// from the dialog's. All methods must be called from the UI/main thread (Qt
// Network is main-thread; download signals are delivered there).
class ModelDownloadCoordinator : public QObject {
    Q_OBJECT
public:
    static ModelDownloadCoordinator* instance();

    // No-op if id already has an active downloader. The entry supplies id +
    // file list; dir + baseUrl come from the caller (the view resolves the
    // mirror so the coordinator stays settings-agnostic). An in-flight
    // download keeps its original baseUrl; changing the mirror only affects
    // the next start().
    void start(const ModelEntry& entry, const QString& dir, const QString& baseUrl);
    void cancel(const QString& id);

    DownloadSnapshot snapshot(const QString& id) const;

signals:
    // Both carry the model id so each ModelRowWidget keeps only its own.
    void progress(const QString& id, qint64 done, qint64 total, const QString& file);
    void stateChanged(const QString& id, DownloadState state, const QString& error);

private:
    explicit ModelDownloadCoordinator(QObject* parent = nullptr);

    QHash<QString, ModelDownloader*> active_;
    QHash<QString, DownloadSnapshot> cache_;
};

}  // namespace echoflow

#endif  // ECHOFLOW_MODEL_DOWNLOAD_COORDINATOR_H
