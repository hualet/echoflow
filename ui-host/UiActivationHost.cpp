// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "UiActivationHost.h"

#include <QMetaObject>
#include <QMutexLocker>

#include <utility>

UiActivationHost::UiActivationHost(QString socketPath, QObject *parent)
    : QObject(parent)
    , socketPath_(std::move(socketPath))
    , workerContext_(new QObject)
{
    workerThread_.setObjectName(QStringLiteral("echoflow-ui-activation"));
    workerContext_->moveToThread(&workerThread_);
    connect(&workerThread_, &QThread::finished,
            workerContext_, &QObject::deleteLater);
    workerThread_.start();

    const bool invoked = QMetaObject::invokeMethod(
        workerContext_,
        [this] {
            server_ = new UiActivationServer(socketPath_, workerContext_);
            connect(server_, &UiActivationServer::activateRequested,
                    this, &UiActivationHost::activateRequested,
                    Qt::QueuedConnection);
        },
        Qt::BlockingQueuedConnection);
    if (!invoked || !server_) {
        initializationError_ = QStringLiteral(
            "Failed to initialize the UI activation worker");
    }
}

UiActivationHost::~UiActivationHost()
{
    QMutexLocker locker(&acquireMutex_);
    if (workerContext_ && workerThread_.isRunning()) {
        QMetaObject::invokeMethod(
            workerContext_,
            [this] {
                delete server_;
                server_ = nullptr;
            },
            Qt::BlockingQueuedConnection);
        workerContext_->deleteLater();
        workerThread_.quit();
        workerThread_.wait();
    }
    workerContext_ = nullptr;
    server_ = nullptr;
}

UiActivationServer::Result UiActivationHost::acquire(bool requestActivation,
                                                      QString *error)
{
    QMutexLocker locker(&acquireMutex_);
    if (error) {
        error->clear();
    }
    if (acquireCalled_) {
        if (error) {
            *error = QStringLiteral("UI activation host acquire may only be called once");
        }
        return UiActivationServer::Result::Failed;
    }
    acquireCalled_ = true;

    if (!initializationError_.isEmpty() || !workerContext_ ||
        !workerThread_.isRunning() || !server_) {
        if (error) {
            *error = initializationError_.isEmpty()
                ? QStringLiteral("UI activation worker is not running")
                : initializationError_;
        }
        return UiActivationServer::Result::Failed;
    }

    UiActivationServer::Result result = UiActivationServer::Result::Failed;
    QString workerError;
    const bool invoked = QMetaObject::invokeMethod(
        workerContext_,
        [this, requestActivation, &result, &workerError] {
            result = server_->acquire(requestActivation, &workerError);
        },
        Qt::BlockingQueuedConnection);
    if (!invoked) {
        if (error) {
            *error = QStringLiteral("Failed to invoke UI activation worker acquire");
        }
        return UiActivationServer::Result::Failed;
    }
    if (error) {
        *error = workerError;
    }
    return result;
}
