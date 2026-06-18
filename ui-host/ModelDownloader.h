// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_MODEL_DOWNLOADER_H
#define ECHOFLOW_MODEL_DOWNLOADER_H

#include "ModelCatalog.h"

#include <QObject>
#include <QString>

#include <cstddef>
#include <string>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;

namespace echoflow {

// Downloads every missing file of one ModelEntry into targetDir via Qt6 Network.
// Files already present at their final name are skipped. Before downloading,
// issues HEAD requests for all pending files so the aggregate byte total is
// known up front — otherwise the small JSON files would each push progress to
// 100% before the large shards begin. Each in-flight file is written to
// <dir>/.<file>.part and renamed on completion; stale .part files are removed
// before fetching. Sequential downloads.
class ModelDownloader : public QObject {
    Q_OBJECT
public:
    ModelDownloader(const ModelEntry& entry,
                    const QString& targetDir,
                    const QString& baseUrl,
                    QObject* parent = nullptr);
    ~ModelDownloader() override;

    void start();
    void cancel();

signals:
    void progress(qint64 done, qint64 total, const QString& currentFile);
    void finished(bool ok, const QString& error);

private:
    void beginSizing();
    void fetchNext();
    QString urlFor(const std::string& file) const;

    ModelEntry entry_;
    QString targetDir_;
    QString baseUrl_;
    QNetworkAccessManager* nam_ = nullptr;
    QNetworkReply* reply_ = nullptr;

    std::vector<std::string> pending_;
    size_t currentIndex_ = 0;
    QString currentFile_;
    qint64 completedBytes_ = 0;    // sum of fully-downloaded files
    qint64 currentReceived_ = 0;   // bytes received for the current file
    qint64 bytesTotalKnown_ = 0;   // sum of Content-Length across files (pre-flight)
    qint64 bytesTotalUnknown_ = 0; // set to 1 if any file's size could not be determined
    std::vector<qint64> fileSizes_; // per pending_ file: sized length, or -1 if unknown
    int sizesRemaining_ = 0;       // outstanding probe requests during the sizing phase
    std::vector<QNetworkReply*> sizeReplies_;
    bool cancelled_ = false;       // set by cancel() before abort; gates finished()
    QString fileError_;            // non-empty if a local write failed
};

}  // namespace echoflow

#endif  // ECHOFLOW_MODEL_DOWNLOADER_H
