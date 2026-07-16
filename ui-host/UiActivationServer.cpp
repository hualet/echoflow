// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "UiActivationServer.h"

#include <QFileInfo>
#include <QLocalSocket>

#include <optional>
#include <utility>

namespace {

constexpr int kConnectionTimeoutMs = 1000;
constexpr char kActivationRequest[] = "ACTIVATE\n";

} // namespace

UiActivationServer::UiActivationServer(QString socketPath, QObject *parent)
    : QObject(parent)
    , socketPath_(std::move(socketPath))
{
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&server_, &QLocalServer::newConnection,
            this, &UiActivationServer::acceptPendingConnections);
}

UiActivationServer::Result UiActivationServer::acquire(bool requestActivation,
                                                        QString *error)
{
    if (error) {
        error->clear();
    }

    QString connectionError;
    const auto contactExisting = [&]() -> std::optional<Result> {
        QLocalSocket socket;
        socket.connectToServer(socketPath_);
        if (!socket.waitForConnected(kConnectionTimeoutMs)) {
            connectionError = socket.errorString();
            return std::nullopt;
        }

        if (!requestActivation) {
            return Result::ActivatedExisting;
        }

        const QByteArray request(kActivationRequest);
        if (socket.write(request) != request.size()) {
            if (error) {
                *error = QStringLiteral("Failed to send activation request to %1: %2")
                             .arg(socketPath_, socket.errorString());
            }
            return Result::Failed;
        }
        while (socket.bytesToWrite() > 0) {
            if (!socket.waitForBytesWritten(kConnectionTimeoutMs)) {
                if (error) {
                    *error = QStringLiteral("Failed to flush activation request to %1: %2")
                                 .arg(socketPath_, socket.errorString());
                }
                return Result::Failed;
            }
        }
        return Result::ActivatedExisting;
    };

    if (QFileInfo::exists(socketPath_)) {
        if (const std::optional<Result> result = contactExisting()) {
            return *result;
        }
        if (!QLocalServer::removeServer(socketPath_)) {
            if (error) {
                *error = QStringLiteral(
                             "Failed to remove stale UI socket %1 after connection error: %2")
                             .arg(socketPath_, connectionError);
            }
            return Result::Failed;
        }
    }

    if (server_.listen(socketPath_)) {
        return Result::Primary;
    }

    const QString initialListenError = server_.errorString();
    if (const std::optional<Result> result = contactExisting()) {
        return *result;
    }

    if (!QLocalServer::removeServer(socketPath_)) {
        if (error) {
            *error = QStringLiteral(
                         "Failed to remove stale UI socket %1 after listen error (%2) "
                         "and connection error (%3)")
                         .arg(socketPath_, initialListenError, connectionError);
        }
        return Result::Failed;
    }

    if (server_.listen(socketPath_)) {
        return Result::Primary;
    }

    if (error) {
        *error = QStringLiteral(
                     "Failed to listen on UI socket %1 after removing stale path: %2 "
                     "(initial error: %3; connection error: %4)")
                     .arg(socketPath_, server_.errorString(), initialListenError,
                          connectionError);
    }
    return Result::Failed;
}

void UiActivationServer::acceptPendingConnections()
{
    while (server_.hasPendingConnections()) {
        QLocalSocket *socket = server_.nextPendingConnection();
        if (!socket) {
            continue;
        }

        buffers_.insert(socket, QByteArray());
        connect(socket, &QLocalSocket::readyRead, this,
                [this, socket] { readSocket(socket); });
        connect(socket, &QLocalSocket::disconnected, socket,
                &QLocalSocket::deleteLater);
        connect(socket, &QObject::destroyed, this,
                [this, socket] { buffers_.remove(socket); });
        readSocket(socket);
    }
}

void UiActivationServer::readSocket(QLocalSocket *socket)
{
    auto it = buffers_.find(socket);
    if (it == buffers_.end()) {
        return;
    }

    it.value().append(socket->readAll());
    QByteArray &buffer = it.value();
    qsizetype newline = -1;
    while ((newline = buffer.indexOf('\n')) >= 0) {
        const QByteArray line = buffer.left(newline);
        buffer.remove(0, newline + 1);
        if (line == QByteArrayLiteral("ACTIVATE")) {
            emit activateRequested();
        }
    }
}
