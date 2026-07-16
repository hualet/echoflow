// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "UiActivationServer.h"

#include <QFile>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>

class TestUiActivationServer : public QObject {
    Q_OBJECT

private slots:
    void firstInstanceBecomesPrimary();
    void secondInstanceRequestsActivation();
    void secondInstanceCanSkipActivation();
    void recoversStaleFilesystemSocket();
    void buffersPartialLinesAndIgnoresUnknownInput();
};

void TestUiActivationServer::firstInstanceBecomesPrimary()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    UiActivationServer server(dir.filePath(QStringLiteral("ui.sock")));
    QString error;
    QCOMPARE(server.acquire(false, &error), UiActivationServer::Result::Primary);
    QVERIFY2(error.isEmpty(), qPrintable(error));
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

QTEST_GUILESS_MAIN(TestUiActivationServer)
#include "test_ui_activation_server.moc"
