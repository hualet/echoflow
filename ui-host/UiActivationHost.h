// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "UiActivationServer.h"

#include <QMutex>
#include <QObject>
#include <QString>
#include <QThread>

class UiActivationHost : public QObject {
    Q_OBJECT

public:
    explicit UiActivationHost(QString socketPath, QObject *parent = nullptr);
    ~UiActivationHost() override;

    UiActivationServer::Result acquire(bool requestActivation,
                                       QString *error = nullptr);

signals:
    void activateRequested();

private:
    QString socketPath_;
    QThread workerThread_;
    QObject *workerContext_ = nullptr;
    UiActivationServer *server_ = nullptr;
    QMutex acquireMutex_;
    QString initializationError_;
    bool acquireCalled_ = false;
};
