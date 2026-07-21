// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "UiActivationServer.h"

#include <QElapsedTimer>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <thread>

namespace {

struct ActivationAttempt {
    UiActivationServer::Result result = UiActivationServer::Result::Failed;
    QString error;
    qint64 durationMs = 0;
};

class ScopedThread {
public:
    explicit ScopedThread(QThread *thread)
        : thread_(thread)
    {
    }

    ~ScopedThread()
    {
        if (thread_) {
            thread_->wait();
            delete thread_;
        }
    }

    QThread *get() const { return thread_; }
    void start() { thread_->start(); }
    bool wait(unsigned long timeoutMs) { return thread_->wait(timeoutMs); }

    ScopedThread(const ScopedThread &) = delete;
    ScopedThread &operator=(const ScopedThread &) = delete;

private:
    QThread *thread_ = nullptr;
};

QThread *startActivationAttempt(const QString &path, bool requestActivation,
                                ActivationAttempt *attempt)
{
    return QThread::create([path, requestActivation, attempt] {
        UiActivationServer secondary(path);
        QElapsedTimer elapsed;
        elapsed.start();
        attempt->result = secondary.acquire(requestActivation, &attempt->error);
        attempt->durationMs = elapsed.elapsed();
    });
}

} // namespace

class TestUiActivationServer : public QObject {
    Q_OBJECT

private slots:
    void firstInstanceBecomesPrimary();
    void secondInstanceRequestsActivation();
    void secondInstanceCanSkipActivation();
    void rejectsLegacyPeerWithoutAcknowledgement();
    void rejectsBadOrClosedAcknowledgement_data();
    void rejectsBadOrClosedAcknowledgement();
    void acceptsPartialAcknowledgement();
    void rejectsBadDoneResponse_data();
    void rejectsBadDoneResponse();
    void contactOnlyAcceptsLegacyPeerWithoutAcknowledgement();
    void closedRequesterDoesNotActivatePrimary();
    void badReadyDoesNotActivatePrimary();
    void recoversStaleFilesystemSocket();
    void preservesRegularFileAtSocketPath();
    void preservesSymlinkAtSocketPath();
    void preservesSocketOnPermissionDenied();
    void rejectsWorldWritableParentAndPreservesSocket();
    void acquisitionLockHonorsTotalDeadline();
    void concurrentAcquisitionHasOnePrimary();
    void buffersPartialActivationAndReady();
    void disconnectsOversizedFragmentedRequest();
    void disconnectsIdleIncompleteClient();
};

void TestUiActivationServer::firstInstanceBecomesPrimary()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    UiActivationServer server(dir.filePath(QStringLiteral("ui.sock")));
    QString error;
    QCOMPARE(server.acquire(false, &error), UiActivationServer::Result::Primary);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    const QByteArray encodedPath = QFile::encodeName(
        dir.filePath(QStringLiteral("ui.sock")));
    struct stat entry {};
    QCOMPARE(::lstat(encodedPath.constData(), &entry), 0);
    QCOMPARE(entry.st_mode & 0777, mode_t(0600));
}

void TestUiActivationServer::secondInstanceRequestsActivation()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ui.sock"));

    UiActivationServer primary(path);
    QCOMPARE(primary.acquire(false), UiActivationServer::Result::Primary);
    QSignalSpy activationSpy(&primary, &UiActivationServer::activateRequested);

    ActivationAttempt attempt;
    ScopedThread secondary(startActivationAttempt(path, true, &attempt));
    QSignalSpy finishedSpy(secondary.get(), &QThread::finished);
    secondary.start();
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1500);
    QVERIFY(secondary.wait(100));

    QCOMPARE(attempt.result, UiActivationServer::Result::ActivatedExisting);
    QVERIFY2(attempt.error.isEmpty(), qPrintable(attempt.error));
    QTRY_COMPARE(activationSpy.count(), 1);
}

void TestUiActivationServer::secondInstanceCanSkipActivation()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ui.sock"));

    UiActivationServer primary(path);
    QCOMPARE(primary.acquire(false), UiActivationServer::Result::Primary);
    QSignalSpy activationSpy(&primary, &UiActivationServer::activateRequested);

    UiActivationServer secondary(path);
    QCOMPARE(secondary.acquire(false),
             UiActivationServer::Result::ActivatedExisting);
    QTest::qWait(50);
    QCOMPARE(activationSpy.count(), 0);
}

void TestUiActivationServer::rejectsLegacyPeerWithoutAcknowledgement()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ui.sock"));
    QLocalServer legacy;
    legacy.setSocketOptions(QLocalServer::UserAccessOption);
    QVERIFY(legacy.listen(path));
    const QByteArray encodedPath = QFile::encodeName(path);
    struct stat before {};
    QCOMPARE(::lstat(encodedPath.constData(), &before), 0);
    connect(&legacy, &QLocalServer::newConnection, &legacy, [&legacy] {
        while (legacy.hasPendingConnections()) {
            QLocalSocket *peer = legacy.nextPendingConnection();
            QObject::connect(peer, &QLocalSocket::readyRead, peer,
                             [peer] { peer->readAll(); });
        }
    });

    ActivationAttempt attempt;
    ScopedThread secondary(startActivationAttempt(path, true, &attempt));
    QSignalSpy finishedSpy(secondary.get(), &QThread::finished);
    secondary.start();
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1500);
    QVERIFY(secondary.wait(100));

    QCOMPARE(attempt.result, UiActivationServer::Result::Failed);
    QVERIFY2(attempt.error.contains(QStringLiteral("ACK"), Qt::CaseInsensitive) ||
                 attempt.error.contains(QStringLiteral("protocol"), Qt::CaseInsensitive),
             qPrintable(attempt.error));
    QVERIFY(attempt.durationMs >= 900);
    QVERIFY(attempt.durationMs < 1500);
    struct stat after {};
    QCOMPARE(::lstat(encodedPath.constData(), &after), 0);
    QCOMPARE(after.st_dev, before.st_dev);
    QCOMPARE(after.st_ino, before.st_ino);
    QVERIFY(legacy.isListening());
}

void TestUiActivationServer::rejectsBadOrClosedAcknowledgement_data()
{
    QTest::addColumn<QByteArray>("reply");
    QTest::addColumn<QByteArray>("delayedReply");
    QTest::addColumn<bool>("closeWithoutReply");

    QTest::newRow("bad-ack")
        << QByteArray("NOPE\n") << QByteArray() << false;
    QTest::newRow("ack-with-trailing-garbage")
        << QByteArray("ACK\nNOPE") << QByteArray() << false;
    QTest::newRow("ack-with-delayed-trailing-garbage")
        << QByteArray("ACK\n") << QByteArray("NOPE") << false;
    QTest::newRow("closed")
        << QByteArray() << QByteArray() << true;
}

void TestUiActivationServer::rejectsBadOrClosedAcknowledgement()
{
    QFETCH(QByteArray, reply);
    QFETCH(QByteArray, delayedReply);
    QFETCH(bool, closeWithoutReply);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ui.sock"));
    QLocalServer fakePeer;
    fakePeer.setSocketOptions(QLocalServer::UserAccessOption);
    QVERIFY(fakePeer.listen(path));
    const QByteArray encodedPath = QFile::encodeName(path);
    struct stat before {};
    QCOMPARE(::lstat(encodedPath.constData(), &before), 0);
    connect(&fakePeer, &QLocalServer::newConnection, &fakePeer,
            [&fakePeer, reply, delayedReply, closeWithoutReply] {
        while (fakePeer.hasPendingConnections()) {
            QLocalSocket *peer = fakePeer.nextPendingConnection();
            QObject::connect(peer, &QLocalSocket::readyRead, peer,
                             [peer, reply, delayedReply, closeWithoutReply] {
                peer->readAll();
                if (closeWithoutReply) {
                    peer->abort();
                    return;
                }
                peer->write(reply);
                peer->flush();
                if (!delayedReply.isEmpty()) {
                    QTimer::singleShot(10, peer, [peer, delayedReply] {
                        peer->write(delayedReply);
                        peer->flush();
                        peer->disconnectFromServer();
                    });
                } else {
                    peer->disconnectFromServer();
                }
            });
        }
    });

    ActivationAttempt attempt;
    ScopedThread secondary(startActivationAttempt(path, true, &attempt));
    QSignalSpy finishedSpy(secondary.get(), &QThread::finished);
    secondary.start();
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1500);
    QVERIFY(secondary.wait(100));

    QCOMPARE(attempt.result, UiActivationServer::Result::Failed);
    QVERIFY2(attempt.error.contains(QStringLiteral("ACK"), Qt::CaseInsensitive) ||
                 attempt.error.contains(QStringLiteral("protocol"), Qt::CaseInsensitive),
             qPrintable(attempt.error));
    struct stat after {};
    QCOMPARE(::lstat(encodedPath.constData(), &after), 0);
    QCOMPARE(after.st_dev, before.st_dev);
    QCOMPARE(after.st_ino, before.st_ino);
    QVERIFY(fakePeer.isListening());
}

void TestUiActivationServer::acceptsPartialAcknowledgement()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ui.sock"));
    QLocalServer fakePeer;
    fakePeer.setSocketOptions(QLocalServer::UserAccessOption);
    QVERIFY(fakePeer.listen(path));
    connect(&fakePeer, &QLocalServer::newConnection, &fakePeer, [&fakePeer] {
        while (fakePeer.hasPendingConnections()) {
            QLocalSocket *peer = fakePeer.nextPendingConnection();
            auto buffer = std::make_shared<QByteArray>();
            auto awaitingReady = std::make_shared<bool>(false);
            QObject::connect(peer, &QLocalSocket::readyRead, peer,
                             [peer, buffer, awaitingReady] {
                buffer->append(peer->readAll());
                if (!*awaitingReady && *buffer == QByteArray("ACTIVATE\n")) {
                    buffer->clear();
                    *awaitingReady = true;
                    peer->write("A");
                    peer->flush();
                    QTimer::singleShot(10, peer, [peer] {
                        peer->write("CK\n");
                        peer->flush();
                    });
                    return;
                }
                if (*awaitingReady && *buffer == QByteArray("READY\n")) {
                    peer->write("DO");
                    peer->flush();
                    QTimer::singleShot(10, peer, [peer] {
                        peer->write("NE\n");
                        peer->flush();
                        peer->disconnectFromServer();
                    });
                }
            });
        }
    });

    ActivationAttempt attempt;
    ScopedThread secondary(startActivationAttempt(path, true, &attempt));
    QSignalSpy finishedSpy(secondary.get(), &QThread::finished);
    secondary.start();
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1500);
    QVERIFY(secondary.wait(100));

    QCOMPARE(attempt.result, UiActivationServer::Result::ActivatedExisting);
    QVERIFY2(attempt.error.isEmpty(), qPrintable(attempt.error));
}

void TestUiActivationServer::rejectsBadDoneResponse_data()
{
    QTest::addColumn<QByteArray>("reply");

    QTest::newRow("bad-done") << QByteArray("NOPE\n");
    QTest::newRow("done-with-trailing-garbage")
        << QByteArray("DONE\nNOPE");
    QTest::newRow("missing-done") << QByteArray();
}

void TestUiActivationServer::rejectsBadDoneResponse()
{
    QFETCH(QByteArray, reply);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ui.sock"));
    QLocalServer fakePeer;
    fakePeer.setSocketOptions(QLocalServer::UserAccessOption);
    QVERIFY(fakePeer.listen(path));
    connect(&fakePeer, &QLocalServer::newConnection, &fakePeer,
            [&fakePeer, reply] {
        while (fakePeer.hasPendingConnections()) {
            QLocalSocket *peer = fakePeer.nextPendingConnection();
            auto buffer = std::make_shared<QByteArray>();
            QObject::connect(peer, &QLocalSocket::readyRead, peer,
                             [peer, buffer, reply] {
                buffer->append(peer->readAll());
                if (*buffer == QByteArray("ACTIVATE\n")) {
                    buffer->clear();
                    peer->write("ACK\n");
                    peer->flush();
                } else if (*buffer == QByteArray("READY\n")) {
                    if (!reply.isEmpty()) {
                        peer->write(reply);
                        peer->flush();
                    }
                    peer->disconnectFromServer();
                }
            });
        }
    });

    ActivationAttempt attempt;
    ScopedThread secondary(startActivationAttempt(path, true, &attempt));
    QSignalSpy finishedSpy(secondary.get(), &QThread::finished);
    secondary.start();
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1500);
    QVERIFY(secondary.wait(100));

    QCOMPARE(attempt.result, UiActivationServer::Result::Failed);
    QVERIFY2(attempt.error.contains(QStringLiteral("DONE"), Qt::CaseInsensitive) ||
                 attempt.error.contains(QStringLiteral("protocol"), Qt::CaseInsensitive),
             qPrintable(attempt.error));
    QVERIFY(fakePeer.isListening());
}

void TestUiActivationServer::contactOnlyAcceptsLegacyPeerWithoutAcknowledgement()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ui.sock"));
    QLocalServer legacy;
    legacy.setSocketOptions(QLocalServer::UserAccessOption);
    QVERIFY(legacy.listen(path));

    ActivationAttempt attempt;
    ScopedThread secondary(startActivationAttempt(path, false, &attempt));
    QSignalSpy finishedSpy(secondary.get(), &QThread::finished);
    secondary.start();
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 500);
    QVERIFY(secondary.wait(100));

    QCOMPARE(attempt.result, UiActivationServer::Result::ActivatedExisting);
    QVERIFY2(attempt.error.isEmpty(), qPrintable(attempt.error));
}

void TestUiActivationServer::closedRequesterDoesNotActivatePrimary()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ui.sock"));
    UiActivationServer primary(path);
    QCOMPARE(primary.acquire(false), UiActivationServer::Result::Primary);
    QSignalSpy activationSpy(&primary, &UiActivationServer::activateRequested);

    QLocalSocket client;
    client.connectToServer(path);
    QVERIFY(client.waitForConnected(1000));
    QCOMPARE(client.write("ACTIVATE\n"), qint64(9));
    QVERIFY(client.waitForBytesWritten(1000));
    QTRY_VERIFY_WITH_TIMEOUT(client.bytesAvailable() >= 4, 500);
    QCOMPARE(client.readAll(), QByteArray("ACK\n"));
    client.abort();

    QTest::qWait(100);
    QCOMPARE(activationSpy.count(), 0);
}

void TestUiActivationServer::badReadyDoesNotActivatePrimary()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ui.sock"));
    UiActivationServer primary(path);
    QCOMPARE(primary.acquire(false), UiActivationServer::Result::Primary);
    QSignalSpy activationSpy(&primary, &UiActivationServer::activateRequested);

    QLocalSocket client;
    client.connectToServer(path);
    QVERIFY(client.waitForConnected(1000));
    QCOMPARE(client.write("ACTIVATE\n"), qint64(9));
    QVERIFY(client.waitForBytesWritten(1000));
    QTRY_VERIFY_WITH_TIMEOUT(client.bytesAvailable() >= 4, 500);
    QCOMPARE(client.readAll(), QByteArray("ACK\n"));
    QCOMPARE(client.write("NOPE\n"), qint64(5));
    QVERIFY(client.waitForBytesWritten(1000));

    QTRY_COMPARE_WITH_TIMEOUT(client.state(), QLocalSocket::UnconnectedState, 500);
    QCOMPARE(activationSpy.count(), 0);
}

void TestUiActivationServer::recoversStaleFilesystemSocket()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("stale.sock"));
    const QByteArray encodedPath = QFile::encodeName(path);
    QVERIFY(encodedPath.size() < static_cast<int>(sizeof(sockaddr_un::sun_path)));

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    QVERIFY(fd >= 0);
    sockaddr_un address {};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, encodedPath.constData(),
                static_cast<size_t>(encodedPath.size() + 1));
    QCOMPARE(::bind(fd, reinterpret_cast<const sockaddr *>(&address),
                    sizeof(address)), 0);
    QCOMPARE(::close(fd), 0);
    QVERIFY(QFile::exists(path));

    UiActivationServer server(path);
    QString error;
    QCOMPARE(server.acquire(false, &error), UiActivationServer::Result::Primary);
    QVERIFY2(error.isEmpty(), qPrintable(error));
}

void TestUiActivationServer::preservesRegularFileAtSocketPath()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ui.sock"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("keep"), qint64(4));
    file.close();

    UiActivationServer server(path);
    QString error;
    QCOMPARE(server.acquire(false, &error), UiActivationServer::Result::Failed);
    QVERIFY2(error.contains(QStringLiteral("socket"), Qt::CaseInsensitive),
             qPrintable(error));
    QVERIFY(QFile::exists(path));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("keep"));
}

void TestUiActivationServer::preservesSymlinkAtSocketPath()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString target = dir.filePath(QStringLiteral("target"));
    QFile targetFile(target);
    QVERIFY(targetFile.open(QIODevice::WriteOnly));
    QCOMPARE(targetFile.write("keep"), qint64(4));
    targetFile.close();

    const QString path = dir.filePath(QStringLiteral("ui.sock"));
    const QByteArray encodedTarget = QFile::encodeName(target);
    const QByteArray encodedPath = QFile::encodeName(path);
    QCOMPARE(::symlink(encodedTarget.constData(), encodedPath.constData()), 0);

    UiActivationServer server(path);
    QString error;
    QCOMPARE(server.acquire(false, &error), UiActivationServer::Result::Failed);
    QVERIFY2(error.contains(QStringLiteral("symlink"), Qt::CaseInsensitive),
             qPrintable(error));

    struct stat entry {};
    QCOMPARE(::lstat(encodedPath.constData(), &entry), 0);
    QVERIFY(S_ISLNK(entry.st_mode));
    QVERIFY(QFile::exists(target));
}

void TestUiActivationServer::preservesSocketOnPermissionDenied()
{
    if (::geteuid() == 0) {
        QSKIP("Socket permission denial requires a non-root test process");
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("protected.sock"));
    const QByteArray encodedPath = QFile::encodeName(path);

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    QVERIFY(fd >= 0);
    sockaddr_un address {};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, encodedPath.constData(),
                static_cast<size_t>(encodedPath.size() + 1));
    QCOMPARE(::bind(fd, reinterpret_cast<const sockaddr *>(&address),
                    sizeof(address)), 0);
    QCOMPARE(::listen(fd, 1), 0);
    QCOMPARE(::chmod(encodedPath.constData(), 0), 0);

    struct stat before {};
    QCOMPARE(::lstat(encodedPath.constData(), &before), 0);
    UiActivationServer server(path);
    QString error;
    QCOMPARE(server.acquire(false, &error), UiActivationServer::Result::Failed);
    QVERIFY2(!error.isEmpty(), qPrintable(error));

    struct stat after {};
    QCOMPARE(::lstat(encodedPath.constData(), &after), 0);
    QCOMPARE(after.st_dev, before.st_dev);
    QCOMPARE(after.st_ino, before.st_ino);
    QCOMPARE(::close(fd), 0);
    QCOMPARE(::unlink(encodedPath.constData()), 0);
}

void TestUiActivationServer::rejectsWorldWritableParentAndPreservesSocket()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString unsafeParent = dir.filePath(QStringLiteral("unsafe"));
    QVERIFY(QDir().mkpath(unsafeParent));
    const QByteArray encodedParent = QFile::encodeName(unsafeParent);
    QCOMPARE(::chmod(encodedParent.constData(), 0777), 0);

    const QString path = unsafeParent + QStringLiteral("/ui.sock");
    const QByteArray encodedPath = QFile::encodeName(path);
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    QVERIFY(fd >= 0);
    sockaddr_un address {};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, encodedPath.constData(),
                static_cast<size_t>(encodedPath.size() + 1));
    QCOMPARE(::bind(fd, reinterpret_cast<const sockaddr *>(&address),
                    sizeof(address)), 0);
    QCOMPARE(::close(fd), 0);

    struct stat before {};
    QCOMPARE(::lstat(encodedPath.constData(), &before), 0);
    UiActivationServer server(path);
    QString error;
    QCOMPARE(server.acquire(false, &error), UiActivationServer::Result::Failed);
    QVERIFY2(error.contains(QStringLiteral("writable"), Qt::CaseInsensitive),
             qPrintable(error));

    struct stat after {};
    QCOMPARE(::lstat(encodedPath.constData(), &after), 0);
    QCOMPARE(after.st_dev, before.st_dev);
    QCOMPARE(after.st_ino, before.st_ino);
    QCOMPARE(::unlink(encodedPath.constData()), 0);
    QCOMPARE(::chmod(encodedParent.constData(), 0700), 0);
}

void TestUiActivationServer::acquisitionLockHonorsTotalDeadline()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ui.sock"));
    const QByteArray encodedLockPath =
        QFile::encodeName(path + QStringLiteral(".lock"));
    std::atomic<bool> lockReady {false};
    std::atomic<bool> releaseLock {false};
    std::atomic<int> helperError {0};

    ScopedThread helper(QThread::create([&] {
        const int fd = ::open(encodedLockPath.constData(),
                              O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd < 0 || ::flock(fd, LOCK_EX) != 0) {
            helperError.store(errno);
            if (fd >= 0) {
                ::close(fd);
            }
            lockReady.store(true);
            return;
        }
        lockReady.store(true);
        while (!releaseLock.load()) {
            std::this_thread::yield();
        }
        ::flock(fd, LOCK_UN);
        ::close(fd);
    }));
    helper.start();
    while (!lockReady.load()) {
        std::this_thread::yield();
    }

    UiActivationServer server(path);
    QString error;
    QElapsedTimer elapsed;
    elapsed.start();
    const UiActivationServer::Result result = server.acquire(false, &error);
    const qint64 durationMs = elapsed.elapsed();

    releaseLock.store(true);
    QVERIFY(helper.wait(3000));

    QCOMPARE(helperError.load(), 0);
    QCOMPARE(result, UiActivationServer::Result::Failed);
    QVERIFY2(error.contains(QStringLiteral("Timed out")), qPrintable(error));
    QVERIFY(durationMs >= 900);
    QVERIFY2(durationMs < 1500,
             qPrintable(QStringLiteral("acquire took %1 ms").arg(durationMs)));
}

void TestUiActivationServer::concurrentAcquisitionHasOnePrimary()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ui.sock"));
    std::atomic<int> ready {0};
    std::atomic<int> acquired {0};
    std::atomic<bool> start {false};
    std::array<UiActivationServer::Result, 2> results {
        UiActivationServer::Result::Failed,
        UiActivationServer::Result::Failed,
    };

    const auto acquire = [&](int index) {
        UiActivationServer server(path);
        ready.fetch_add(1);
        while (!start.load()) {
            std::this_thread::yield();
        }
        results[static_cast<size_t>(index)] = server.acquire(false);
        acquired.fetch_add(1);
        while (acquired.load() < 2) {
            std::this_thread::yield();
        }
    };

    ScopedThread first(QThread::create([&] { acquire(0); }));
    ScopedThread second(QThread::create([&] { acquire(1); }));
    first.start();
    second.start();
    while (ready.load() < 2) {
        std::this_thread::yield();
    }
    start.store(true);
    QVERIFY(first.wait(3000));
    QVERIFY(second.wait(3000));

    QCOMPARE(static_cast<int>(std::count(results.cbegin(), results.cend(),
                                         UiActivationServer::Result::Primary)),
             1);
    QCOMPARE(static_cast<int>(std::count(
                 results.cbegin(), results.cend(),
                 UiActivationServer::Result::ActivatedExisting)),
             1);
}

void TestUiActivationServer::buffersPartialActivationAndReady()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ui.sock"));

    UiActivationServer primary(path);
    QCOMPARE(primary.acquire(false), UiActivationServer::Result::Primary);
    QSignalSpy activationSpy(&primary, &UiActivationServer::activateRequested);

    QLocalSocket client;
    client.connectToServer(path);
    QVERIFY(client.waitForConnected(1000));
    QCOMPARE(client.write("ACT"), qint64(3));
    QVERIFY(client.waitForBytesWritten(1000));
    QTest::qWait(20);
    QCOMPARE(activationSpy.count(), 0);

    const QByteArray remaining("IVATE\n");
    QCOMPARE(client.write(remaining), qint64(remaining.size()));
    QVERIFY(client.waitForBytesWritten(1000));
    QTRY_VERIFY_WITH_TIMEOUT(client.bytesAvailable() >= 4, 500);
    QCOMPARE(client.readAll(), QByteArray("ACK\n"));
    QCOMPARE(activationSpy.count(), 0);

    QCOMPARE(client.write("RE"), qint64(2));
    QVERIFY(client.waitForBytesWritten(1000));
    QTest::qWait(20);
    QCOMPARE(activationSpy.count(), 0);
    QCOMPARE(client.write("ADY\n"), qint64(4));
    QVERIFY(client.waitForBytesWritten(1000));
    QTRY_COMPARE(activationSpy.count(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), QLocalSocket::UnconnectedState, 500);
    QCOMPARE(client.readAll(), QByteArray("DONE\n"));
}

void TestUiActivationServer::disconnectsOversizedFragmentedRequest()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ui.sock"));

    UiActivationServer primary(path);
    QCOMPARE(primary.acquire(false), UiActivationServer::Result::Primary);
    QSignalSpy activationSpy(&primary, &UiActivationServer::activateRequested);

    QLocalSocket client;
    client.connectToServer(path);
    QVERIFY(client.waitForConnected(1000));
    const QByteArray fragment(1024, 'X');
    for (int i = 0; i < 5 && client.state() == QLocalSocket::ConnectedState; ++i) {
        QCOMPARE(client.write(fragment), qint64(fragment.size()));
        QVERIFY(client.waitForBytesWritten(1000));
        QCoreApplication::processEvents();
    }

    QTRY_COMPARE_WITH_TIMEOUT(client.state(), QLocalSocket::UnconnectedState, 1000);
    QCOMPARE(activationSpy.count(), 0);
}

void TestUiActivationServer::disconnectsIdleIncompleteClient()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ui.sock"));

    UiActivationServer primary(path);
    QCOMPARE(primary.acquire(false), UiActivationServer::Result::Primary);

    QLocalSocket client;
    client.connectToServer(path);
    QVERIFY(client.waitForConnected(1000));
    QCOMPARE(client.write("ACT"), qint64(3));
    QVERIFY(client.waitForBytesWritten(1000));

    QTRY_COMPARE_WITH_TIMEOUT(client.state(), QLocalSocket::UnconnectedState, 1000);
}

QTEST_GUILESS_MAIN(TestUiActivationServer)
#include "test_ui_activation_server.moc"
