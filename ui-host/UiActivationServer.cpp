// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "UiActivationServer.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLocalSocket>
#include <QTimer>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <optional>
#include <utility>

namespace {

constexpr int kAcquireDeadlineMs = 1000;
constexpr int kAcquireAttempts = 6;
constexpr int kClientIdleTimeoutMs = 250;
constexpr qsizetype kMaximumRequestBuffer = 4096;
constexpr char kActivationRequest[] = "ACTIVATE\n";
constexpr char kActivationAcknowledgement[] = "ACK\n";

struct SocketIdentity {
    dev_t device = 0;
    ino_t inode = 0;
};

enum class PathState {
    Missing,
    Socket,
    Protected,
    Error,
};

struct PathInspection {
    PathState state = PathState::Error;
    SocketIdentity identity;
    QString error;
};

QString systemError(int errorNumber)
{
    return QString::fromLocal8Bit(std::strerror(errorNumber));
}

PathInspection inspectSocketPath(const QString &socketPath)
{
    const QString parentPath = QFileInfo(socketPath).absolutePath();
    const QByteArray encodedParent = QFile::encodeName(parentPath);
    struct stat parent {};
    if (::lstat(encodedParent.constData(), &parent) != 0) {
        return {PathState::Error, {},
                QStringLiteral("Socket parent %1 is unavailable: %2")
                    .arg(parentPath, systemError(errno))};
    }
    if (!S_ISDIR(parent.st_mode) || parent.st_uid != ::getuid()) {
        return {PathState::Protected, {},
                QStringLiteral("Socket parent %1 must be a directory owned by uid %2")
                    .arg(parentPath)
                    .arg(::getuid())};
    }
    if ((parent.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        return {PathState::Protected, {},
                QStringLiteral("Socket parent %1 must not be group/world-writable")
                    .arg(parentPath)};
    }
    if (::access(encodedParent.constData(), W_OK | X_OK) != 0) {
        return {PathState::Protected, {},
                QStringLiteral("Socket parent %1 is not removable by uid %2: %3")
                    .arg(parentPath)
                    .arg(::getuid())
                    .arg(systemError(errno))};
    }

    const QByteArray encodedPath = QFile::encodeName(socketPath);
    struct stat entry {};
    if (::lstat(encodedPath.constData(), &entry) != 0) {
        if (errno == ENOENT) {
            return {PathState::Missing, {}, {}};
        }
        return {PathState::Error, {},
                QStringLiteral("Cannot inspect UI endpoint %1: %2")
                    .arg(socketPath, systemError(errno))};
    }
    if (S_ISLNK(entry.st_mode)) {
        return {PathState::Protected, {},
                QStringLiteral("Refusing to replace symlink at UI socket path %1")
                    .arg(socketPath)};
    }
    if (!S_ISSOCK(entry.st_mode)) {
        return {PathState::Protected, {},
                QStringLiteral("Refusing to replace non-socket entry at UI socket path %1")
                    .arg(socketPath)};
    }
    if (entry.st_uid != ::getuid()) {
        return {PathState::Protected, {},
                QStringLiteral("Refusing to replace UI socket %1 owned by uid %2")
                    .arg(socketPath)
                    .arg(entry.st_uid)};
    }
    return {PathState::Socket, {entry.st_dev, entry.st_ino}, {}};
}

bool sameSocket(const PathInspection &inspection, const SocketIdentity &identity)
{
    return inspection.state == PathState::Socket &&
           inspection.identity.device == identity.device &&
           inspection.identity.inode == identity.inode;
}

void unlinkMatchingSocket(const QString &socketPath,
                          const SocketIdentity &identity)
{
    if (!sameSocket(inspectSocketPath(socketPath), identity)) {
        return;
    }
    const QByteArray encodedPath = QFile::encodeName(socketPath);
    ::unlink(encodedPath.constData());
}

class AcquisitionLock {
public:
    ~AcquisitionLock()
    {
        if (descriptor_ >= 0) {
            ::flock(descriptor_, LOCK_UN);
            ::close(descriptor_);
        }
    }

    bool acquire(const QString &path, QElapsedTimer *deadline, QString *error)
    {
        const QByteArray encodedPath = QFile::encodeName(path);
        descriptor_ = ::open(encodedPath.constData(),
                             O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW,
                             S_IRUSR | S_IWUSR);
        if (descriptor_ < 0) {
            if (error) {
                *error = QStringLiteral("Failed to open UI acquisition lock %1: %2")
                             .arg(path, systemError(errno));
            }
            return false;
        }

        struct stat lockEntry {};
        if (::fstat(descriptor_, &lockEntry) != 0 ||
            !S_ISREG(lockEntry.st_mode) || lockEntry.st_uid != ::getuid()) {
            if (error) {
                *error = QStringLiteral(
                             "UI acquisition lock %1 must be a regular file owned by uid %2")
                             .arg(path)
                             .arg(::getuid());
            }
            return false;
        }
        if (::fchmod(descriptor_, S_IRUSR | S_IWUSR) != 0) {
            if (error) {
                *error = QStringLiteral("Failed to secure UI acquisition lock %1: %2")
                             .arg(path, systemError(errno));
            }
            return false;
        }

        while (::flock(descriptor_, LOCK_EX | LOCK_NB) != 0) {
            if (errno != EWOULDBLOCK && errno != EAGAIN) {
                if (error) {
                    *error = QStringLiteral("Failed to lock UI acquisition file %1: %2")
                                 .arg(path, systemError(errno));
                }
                return false;
            }
            if (deadline->elapsed() >= kAcquireDeadlineMs) {
                if (error) {
                    *error = QStringLiteral("Timed out waiting for UI acquisition lock %1")
                                 .arg(path);
                }
                return false;
            }
            ::usleep(5000);
        }
        return true;
    }

private:
    int descriptor_ = -1;
};

} // namespace

UiActivationServer::UiActivationServer(QString socketPath, QObject *parent)
    : QObject(parent)
    , socketPath_(std::move(socketPath))
{
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&server_, &QLocalServer::newConnection,
            this, &UiActivationServer::acceptPendingConnections);
}

UiActivationServer::~UiActivationServer()
{
    server_.close();
    if (!ownsSocketPath_) {
        return;
    }

    const PathInspection inspection = inspectSocketPath(socketPath_);
    const SocketIdentity ownedIdentity {
        static_cast<dev_t>(socketDevice_),
        static_cast<ino_t>(socketInode_),
    };
    if (sameSocket(inspection, ownedIdentity)) {
        const QByteArray encodedPath = QFile::encodeName(socketPath_);
        ::unlink(encodedPath.constData());
    }
}

UiActivationServer::Result UiActivationServer::acquire(bool requestActivation,
                                                        QString *error)
{
    if (error) {
        error->clear();
    }
    QElapsedTimer deadline;
    deadline.start();

    const PathInspection initialInspection = inspectSocketPath(socketPath_);
    if (initialInspection.state == PathState::Protected ||
        initialInspection.state == PathState::Error) {
        if (error) {
            *error = initialInspection.error;
        }
        return Result::Failed;
    }
    AcquisitionLock acquisitionLock;
    if (!acquisitionLock.acquire(socketPath_ + QStringLiteral(".lock"),
                                 &deadline, error)) {
        return Result::Failed;
    }

    const auto remainingTime = [&] {
        return qMax(0, kAcquireDeadlineMs - static_cast<int>(deadline.elapsed()));
    };

    const auto contactExisting = [&](const SocketIdentity &identity)
        -> std::optional<Result> {
        const int connectTimeout = remainingTime();
        if (connectTimeout <= 0) {
            if (error) {
                *error = QStringLiteral("Timed out acquiring UI socket %1")
                             .arg(socketPath_);
            }
            return Result::Failed;
        }

        QLocalSocket socket;
        socket.connectToServer(socketPath_);
        if (!socket.waitForConnected(connectTimeout)) {
            const QLocalSocket::LocalSocketError socketError = socket.error();
            if (socketError == QLocalSocket::ConnectionRefusedError) {
                const PathInspection current = inspectSocketPath(socketPath_);
                if (!sameSocket(current, identity)) {
                    return std::nullopt;
                }
                const QByteArray encodedPath = QFile::encodeName(socketPath_);
                if (::unlink(encodedPath.constData()) == 0 || errno == ENOENT) {
                    return std::nullopt;
                }
                if (error) {
                    *error = QStringLiteral("Failed to remove stale UI socket %1: %2")
                                 .arg(socketPath_, systemError(errno));
                }
                return Result::Failed;
            }
            if (socketError == QLocalSocket::ServerNotFoundError) {
                const PathInspection current = inspectSocketPath(socketPath_);
                if (current.state == PathState::Missing ||
                    (current.state == PathState::Socket &&
                     !sameSocket(current, identity))) {
                    return std::nullopt;
                }
            }
            if (error) {
                *error = QStringLiteral("Failed to contact UI socket %1: %2")
                             .arg(socketPath_, socket.errorString());
            }
            return Result::Failed;
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
            const int writeTimeout = remainingTime();
            if (writeTimeout <= 0 || !socket.waitForBytesWritten(writeTimeout)) {
                if (error) {
                    *error = QStringLiteral("Failed to flush activation request to %1: %2")
                                 .arg(socketPath_, socket.errorString());
                }
                return Result::Failed;
            }
        }

        QByteArray acknowledgement;
        while (true) {
            const qint64 readLimit = kMaximumRequestBuffer + 1 -
                                     acknowledgement.size();
            acknowledgement.append(socket.read(readLimit));
            if (acknowledgement.size() > kMaximumRequestBuffer ||
                socket.bytesAvailable() > 0) {
                if (error) {
                    *error = QStringLiteral(
                                 "Activation ACK protocol response from %1 exceeded %2 bytes")
                                 .arg(socketPath_)
                                 .arg(kMaximumRequestBuffer);
                }
                return Result::Failed;
            }

            const qsizetype newline = acknowledgement.indexOf('\n');
            if (newline >= 0) {
                if (acknowledgement == QByteArray(kActivationAcknowledgement)) {
                    return Result::ActivatedExisting;
                }
                if (error) {
                    *error = QStringLiteral(
                                 "Activation ACK protocol mismatch from %1: expected ACK")
                                 .arg(socketPath_);
                }
                return Result::Failed;
            }
            if (socket.state() == QLocalSocket::UnconnectedState) {
                if (error) {
                    *error = QStringLiteral(
                                 "Activation ACK protocol failed for %1: peer closed without ACK")
                                 .arg(socketPath_);
                }
                return Result::Failed;
            }

            const int acknowledgementTimeout = remainingTime();
            if (acknowledgementTimeout <= 0) {
                if (error) {
                    *error = QStringLiteral(
                                 "Activation ACK protocol failed for %1: "
                                 "timed out waiting for ACK; peer may use an older protocol")
                                 .arg(socketPath_);
                }
                return Result::Failed;
            }
            socket.waitForReadyRead(acknowledgementTimeout);
        }
    };

    for (int attempt = 0; attempt < kAcquireAttempts; ++attempt) {
        if (remainingTime() <= 0) {
            break;
        }

        const PathInspection inspection = inspectSocketPath(socketPath_);
        if (inspection.state == PathState::Protected ||
            inspection.state == PathState::Error) {
            if (error) {
                *error = inspection.error;
            }
            return Result::Failed;
        }
        if (inspection.state == PathState::Socket) {
            if (const std::optional<Result> result =
                    contactExisting(inspection.identity)) {
                return *result;
            }
            continue;
        }

        const QByteArray encodedPath = QFile::encodeName(socketPath_);
        if (encodedPath.size() >= static_cast<int>(sizeof(sockaddr_un::sun_path))) {
            if (error) {
                *error = QStringLiteral("UI socket path is too long: %1").arg(socketPath_);
            }
            return Result::Failed;
        }

        const int descriptor = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (descriptor < 0) {
            if (error) {
                *error = QStringLiteral("Failed to create UI socket: %1")
                             .arg(systemError(errno));
            }
            return Result::Failed;
        }
        sockaddr_un address {};
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, encodedPath.constData(),
                    static_cast<size_t>(encodedPath.size() + 1));
        if (::bind(descriptor, reinterpret_cast<const sockaddr *>(&address),
                   sizeof(address)) != 0) {
            const int bindError = errno;
            ::close(descriptor);
            if (bindError == EADDRINUSE || bindError == ENOENT) {
                continue;
            }
            if (error) {
                *error = QStringLiteral("Failed to bind UI socket %1: %2")
                             .arg(socketPath_, systemError(bindError));
            }
            return Result::Failed;
        }

        const PathInspection boundInspection = inspectSocketPath(socketPath_);
        if (boundInspection.state != PathState::Socket) {
            ::close(descriptor);
            if (error) {
                *error = boundInspection.error.isEmpty()
                    ? QStringLiteral("UI socket path changed while binding %1")
                          .arg(socketPath_)
                    : boundInspection.error;
            }
            return Result::Failed;
        }
        if (::chmod(encodedPath.constData(), S_IRUSR | S_IWUSR) != 0 ||
            ::listen(descriptor, 50) != 0) {
            const int setupError = errno;
            ::close(descriptor);
            unlinkMatchingSocket(socketPath_, boundInspection.identity);
            if (error) {
                *error = QStringLiteral("Failed to prepare UI socket %1: %2")
                             .arg(socketPath_, systemError(setupError));
            }
            return Result::Failed;
        }
        if (!server_.listen(descriptor)) {
            const QString listenError = server_.errorString();
            ::close(descriptor);
            unlinkMatchingSocket(socketPath_, boundInspection.identity);
            if (error) {
                *error = QStringLiteral("Failed to adopt UI socket %1: %2")
                             .arg(socketPath_, listenError);
            }
            return Result::Failed;
        }

        socketDevice_ = static_cast<quint64>(boundInspection.identity.device);
        socketInode_ = static_cast<quint64>(boundInspection.identity.inode);
        ownsSocketPath_ = true;
        return Result::Primary;
    }

    if (error) {
        *error = QStringLiteral("Could not acquire UI socket %1 after bounded retries")
                     .arg(socketPath_);
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

        QTimer *idleTimer = new QTimer(socket);
        idleTimer->setSingleShot(true);
        idleTimer->setInterval(kClientIdleTimeoutMs);
        clients_.insert(socket, {QByteArray(), idleTimer, 0});
        connect(idleTimer, &QTimer::timeout, this, [this, socket] {
            clients_.remove(socket);
            socket->abort();
            socket->deleteLater();
        });
        connect(socket, &QLocalSocket::readyRead, this,
                [this, socket] { readSocket(socket); });
        connect(socket, &QLocalSocket::bytesWritten, this,
                [this, socket] { finishAcknowledgements(socket); },
                Qt::QueuedConnection);
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            clients_.remove(socket);
            socket->deleteLater();
        });
        connect(socket, &QObject::destroyed, this,
                [this, socket] { clients_.remove(socket); });
        idleTimer->start();
        readSocket(socket);
    }
}

void UiActivationServer::finishAcknowledgements(QLocalSocket *socket)
{
    auto it = clients_.find(socket);
    if (it == clients_.end() || socket->bytesToWrite() > 0) {
        return;
    }

    const int completed = std::exchange(it.value().pendingAcknowledgements, 0);
    for (int i = 0; i < completed; ++i) {
        emit activateRequested();
    }
}

void UiActivationServer::readSocket(QLocalSocket *socket)
{
    auto it = clients_.find(socket);
    if (it == clients_.end()) {
        return;
    }

    const qint64 readLimit = kMaximumRequestBuffer + 1 -
                             it.value().buffer.size();
    it.value().buffer.append(socket->read(readLimit));
    if (it.value().buffer.size() > kMaximumRequestBuffer ||
        socket->bytesAvailable() > 0) {
        clients_.erase(it);
        socket->abort();
        socket->deleteLater();
        return;
    }
    it.value().idleTimer->start();

    QByteArray &buffer = it.value().buffer;
    qsizetype newline = -1;
    while ((newline = buffer.indexOf('\n')) >= 0) {
        const QByteArray line = buffer.left(newline);
        buffer.remove(0, newline + 1);
        if (line == QByteArrayLiteral("ACTIVATE")) {
            ++it.value().pendingAcknowledgements;
            const QByteArray acknowledgement(kActivationAcknowledgement);
            if (socket->write(acknowledgement) != acknowledgement.size()) {
                clients_.erase(it);
                socket->abort();
                socket->deleteLater();
                return;
            }
            socket->flush();
            QMetaObject::invokeMethod(
                this, [this, socket] { finishAcknowledgements(socket); },
                Qt::QueuedConnection);
        }
    }
}
