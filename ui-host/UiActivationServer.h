// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QHash>
#include <QLocalServer>
#include <QObject>
#include <QString>

class QLocalSocket;
class QTimer;

class UiActivationServer : public QObject {
    Q_OBJECT

public:
    enum class Result {
        Primary,
        // An existing instance was contacted; an activation payload is optional.
        ActivatedExisting,
        Failed,
    };

    explicit UiActivationServer(QString socketPath, QObject *parent = nullptr);
    ~UiActivationServer() override;

    Result acquire(bool requestActivation, QString *error = nullptr);

signals:
    void activateRequested();

private:
    enum class ClientPhase {
        AwaitActivate,
        AwaitReady,
        SendingDone,
    };

    struct ClientState {
        QByteArray buffer;
        QTimer *idleTimer = nullptr;
        ClientPhase phase = ClientPhase::AwaitActivate;
    };

    void acceptPendingConnections();
    void finishResponse(QLocalSocket *socket);
    void readSocket(QLocalSocket *socket);

    QString socketPath_;
    QHash<QLocalSocket *, ClientState> clients_;
    QLocalServer server_;
    quint64 socketDevice_ = 0;
    quint64 socketInode_ = 0;
    bool ownsSocketPath_ = false;
};
