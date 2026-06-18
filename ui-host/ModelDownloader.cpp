// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ModelDownloader.h"

#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <algorithm>

namespace echoflow {

ModelDownloader::ModelDownloader(const ModelEntry& entry,
                                 const QString& targetDir,
                                 const QString& baseUrl,
                                 QObject* parent)
    : QObject(parent), entry_(entry), targetDir_(targetDir), baseUrl_(baseUrl)
{
    nam_ = new QNetworkAccessManager(this);
}

ModelDownloader::~ModelDownloader() {
    if (reply_) {
        // Disconnect before abort so the finished lambda does not fire on a
        // half-destroyed object.
        disconnect(reply_, nullptr, this, nullptr);
        reply_->abort();
    }
    for (QNetworkReply* h : sizeReplies_) {
        disconnect(h, nullptr, this, nullptr);
        h->abort();
    }
}

QString ModelDownloader::urlFor(const std::string& file) const {
    // {base}/{repo}/resolve/main/{file}
    return baseUrl_ + QStringLiteral("/") +
           QString::fromStdString(entry_.repo) +
           QStringLiteral("/resolve/main/") +
           QString::fromStdString(file);
}

void ModelDownloader::start() {
    QDir().mkpath(targetDir_);
    for (const auto& file : entry_.files) {
        const QString finalPath = targetDir_ + QStringLiteral("/") + QString::fromStdString(file);
        if (!QFile::exists(finalPath)) {
            pending_.push_back(file);
        }
    }
    if (pending_.empty()) {
        emit finished(true, QString());
        return;
    }
    currentIndex_ = 0;
    completedBytes_ = 0;
    currentReceived_ = 0;
    bytesTotalKnown_ = 0;
    bytesTotalUnknown_ = 0;
    beginSizing();
}

void ModelDownloader::beginSizing() {
    // Pre-flight: probe every pending file so the aggregate total is known
    // before the first real byte is downloaded. Without this, the small JSON
    // files (config.json, vocab.json, ...) would each push progress to 100%
    // before the multi-GB shards start, because the denominator only reflected
    // the file currently being transferred.
    //
    // We issue GETs (not HEADs) because some CDNs reject HEAD on the signed
    // redirect URLs HuggingFace/hf-mirror hand out for LFS shards (405). The
    // Content-Length is read once the first body byte arrives (i.e. on the
    // final post-redirect response), then the reply is aborted — only a
    // trivial amount of body data is transferred per file.
    fileSizes_.assign(pending_.size(), -1);
    sizesRemaining_ = static_cast<int>(pending_.size());
    for (size_t i = 0; i < pending_.size(); ++i) {
        QNetworkRequest req(urlFor(pending_[i]));
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("echoflow"));
        QNetworkReply* g = nam_->get(req);
        sizeReplies_.push_back(g);
        QObject::connect(g, &QNetworkReply::readyRead, this, [this, g, i]() {
            if (fileSizes_[i] >= 0) {
                return;  // already sized this file
            }
            bool ok = false;
            const qint64 len = g->header(QNetworkRequest::ContentLengthHeader).toLongLong(&ok);
            if (ok && len > 0) {
                fileSizes_[i] = len;
            }
            g->abort();  // length captured (or unknowable); stop the body transfer
        });
        QObject::connect(g, &QNetworkReply::finished, this, [this, g]() {
            g->deleteLater();
            sizeReplies_.erase(std::remove(sizeReplies_.begin(), sizeReplies_.end(), g),
                               sizeReplies_.end());
            --sizesRemaining_;
            if (cancelled_) {
                return;  // cancel() already emitted finished
            }
            if (sizesRemaining_ == 0) {
                bytesTotalKnown_ = 0;
                bytesTotalUnknown_ = 0;
                for (qint64 s : fileSizes_) {
                    if (s > 0) {
                        bytesTotalKnown_ += s;
                    } else {
                        bytesTotalUnknown_ = 1;
                    }
                }
                currentIndex_ = 0;
                fetchNext();  // total is known; begin downloading
            }
        });
    }
}

void ModelDownloader::cancel() {
    cancelled_ = true;  // gate the finished lambdas before aborting
    if (reply_) {
        // abort() fires finished synchronously (same thread); the lambda sees
        // cancelled_ and returns without emitting.
        reply_->abort();
    }
    for (QNetworkReply* h : sizeReplies_) {
        h->abort();  // sizing replies' finished lambdas see cancelled_ and return
    }
    if (!currentFile_.isEmpty()) {
        QFile::remove(targetDir_ + QStringLiteral("/.") + currentFile_ + QStringLiteral(".part"));
    }
    emit finished(false, QStringLiteral("已取消"));
}

void ModelDownloader::fetchNext() {
    if (currentIndex_ >= pending_.size()) {
        emit finished(true, QString());
        return;
    }
    currentFile_ = QString::fromStdString(pending_[currentIndex_]);
    currentReceived_ = 0;
    fileError_.clear();

    const QString partPath = targetDir_ + QStringLiteral("/.") + currentFile_ + QStringLiteral(".part");
    QFile::remove(partPath);  // stale fragment from a crashed run

    QNetworkRequest req(urlFor(pending_[currentIndex_]));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("echoflow"));
    reply_ = nam_->get(req);

    // readyRead only appends bytes to the .part file; byte accounting lives in
    // downloadProgress (the total was pre-computed during sizing).
    QObject::connect(reply_, &QNetworkReply::readyRead, this, [this, partPath]() {
        if (!fileError_.isEmpty()) {
            return;  // already failed to write; drop further data until abort lands
        }
        const QByteArray chunk = reply_->readAll();
        if (chunk.isEmpty()) {
            return;
        }
        QFile f(partPath);
        if (!f.open(QIODevice::Append)) {
            fileError_ = QStringLiteral("无法写入: ") + partPath;
            reply_->abort();  // finished lambda surfaces fileError_
            return;
        }
        const qint64 written = f.write(chunk);
        f.close();  // flush; deferred disk-full / I/O errors surface via error()
        if (written != chunk.size() || f.error() != QFileDevice::NoError) {
            // Short write or I/O error (e.g. disk full): the .part is truncated.
            // Flag it so the finished handler removes the .part instead of
            // renaming a corrupt file that qwen-asr would fail to load.
            fileError_ = QStringLiteral("写入失败（磁盘已满？）: ") + partPath;
            reply_->abort();
        }
    });

    QObject::connect(reply_, &QNetworkReply::downloadProgress, this,
        [this](qint64 received, qint64 /*total*/) {
            currentReceived_ = received;
            const qint64 done = completedBytes_ + currentReceived_;
            const qint64 effectiveTotal = bytesTotalUnknown_ ? 0 : bytesTotalKnown_;
            emit progress(done, effectiveTotal, currentFile_);
        });

    QObject::connect(reply_, &QNetworkReply::finished, this, [this, partPath]() {
        QNetworkReply::NetworkError err = reply_->error();
        const QString errorString = reply_->errorString();
        reply_->deleteLater();
        reply_ = nullptr;

        if (cancelled_) {
            return;  // cancel() already emitted finished
        }
        if (!fileError_.isEmpty()) {
            QFile::remove(partPath);
            emit finished(false, fileError_);
            return;
        }
        if (err != QNetworkReply::NoError) {
            QFile::remove(partPath);
            emit finished(false, errorString.isEmpty()
                                     ? QStringLiteral("网络错误: ") + QString::number(err)
                                     : errorString);
            return;
        }
        // POSIX rename(2) atomically replaces; start() ensured the final file
        // was absent, so a single rename is sufficient.
        const QString finalPath = targetDir_ + QStringLiteral("/") + currentFile_;
        if (!QFile::rename(partPath, finalPath)) {
            QFile::remove(partPath);
            emit finished(false, QStringLiteral("无法写入: ") + finalPath);
            return;
        }
        completedBytes_ += currentReceived_;
        currentReceived_ = 0;
        ++currentIndex_;
        fetchNext();
    });
}

}  // namespace echoflow
