// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "UiActivationServer.h"

#include <QElapsedTimer>
#include <QFile>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>

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
#include <thread>

class TestUiActivationServer : public QObject {
    Q_OBJECT

private slots:
    void firstInstanceBecomesPrimary();
    void secondInstanceRequestsActivation();
    void secondInstanceCanSkipActivation();
    void recoversStaleFilesystemSocket();
    void preservesRegularFileAtSocketPath();
    void preservesSymlinkAtSocketPath();
    void preservesSocketOnPermissionDenied();
    void rejectsWorldWritableParentAndPreservesSocket();
    void acquisitionLockHonorsTotalDeadline();
    void concurrentAcquisitionHasOnePrimary();
    void buffersPartialLinesAndIgnoresUnknownInput();
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

    UiActivationServer secondary(path);
    QString error;
    QCOMPARE(secondary.acquire(true, &error),
             UiActivationServer::Result::ActivatedExisting);
    QVERIFY2(error.isEmpty(), qPrintable(error));
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

    QThread *helper = QThread::create([&] {
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
    });
    helper->start();
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
    QVERIFY(helper->wait(3000));
    delete helper;

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

    QThread *first = QThread::create([&] { acquire(0); });
    QThread *second = QThread::create([&] { acquire(1); });
    first->start();
    second->start();
    while (ready.load() < 2) {
        std::this_thread::yield();
    }
    start.store(true);
    QVERIFY(first->wait(3000));
    QVERIFY(second->wait(3000));
    delete first;
    delete second;

    QCOMPARE(static_cast<int>(std::count(results.cbegin(), results.cend(),
                                         UiActivationServer::Result::Primary)),
             1);
    QCOMPARE(static_cast<int>(std::count(
                 results.cbegin(), results.cend(),
                 UiActivationServer::Result::ActivatedExisting)),
             1);
}

void TestUiActivationServer::buffersPartialLinesAndIgnoresUnknownInput()
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

    const QByteArray remaining("IVATE\nUNKNOWN\n\nACTIVATE\n");
    QCOMPARE(client.write(remaining), qint64(remaining.size()));
    QVERIFY(client.waitForBytesWritten(1000));
    QTRY_COMPARE(activationSpy.count(), 2);
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
