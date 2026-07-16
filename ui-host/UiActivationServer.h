// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QHash>
#include <QLocalServer>
#include <QObject>
#include <QString>

class QLocalSocket;

class UiActivationServer : public QObject {
    Q_OBJECT

public:
    enum class Result {
        Primary,
        ActivatedExisting,
        Failed,
    };

    explicit UiActivationServer(QString socketPath, QObject *parent = nullptr);

    Result acquire(bool requestActivation, QString *error = nullptr);

signals:
    void activateRequested();

private:
    void acceptPendingConnections();
    void readSocket(QLocalSocket *socket);

    QString socketPath_;
    QHash<QLocalSocket *, QByteArray> buffers_;
    QLocalServer server_;
};
