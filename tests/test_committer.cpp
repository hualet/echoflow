// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "Committer.h"

#include <cerrno>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

using namespace echoflow;

class TestCommitter : public QObject {
    Q_OBJECT

private slots:
    void commitReturnsOkOnAck();
    void commitReturnsErrOnNack();

private:
    std::string serverPath_;
    std::string setupError_;
    int setupServer();
};

int TestCommitter::setupServer()
{
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) {
        setupError_ = "socket errno=" + std::to_string(errno);
        return fd;
    }
    timeval timeout {1, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    serverPath_ = std::string("/tmp/echoflow-test-") + std::to_string(getpid()) + ".sock";
    unlink(serverPath_.c_str());

    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, serverPath_.c_str(), sizeof(addr.sun_path) - 1);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        setupError_ = "bind " + serverPath_ + " errno=" + std::to_string(errno);
        close(fd);
        return -1;
    }
    setupError_.clear();
    return fd;
}

struct ReplyResult {
    ssize_t received = -1;
    int receiveErrno = 0;
    int sent = -1;
    int sendErrno = 0;
    std::string payload;
};

static void recvAndReply(int fd, const std::string& reply, ReplyResult* result)
{
    char buf[512];
    sockaddr_un peer {};
    socklen_t peerLen = sizeof(peer);
    ssize_t n = recvfrom(fd, buf, sizeof(buf) - 1, 0, reinterpret_cast<sockaddr*>(&peer), &peerLen);
    result->received = n;
    result->receiveErrno = errno;
    if (n > 0) {
        result->payload.assign(buf, n);
        result->sent = sendto(fd, reply.data(), reply.size(), 0, reinterpret_cast<sockaddr*>(&peer), peerLen);
        result->sendErrno = errno;
    }
}

void TestCommitter::commitReturnsOkOnAck()
{
    int fd = setupServer();
    QVERIFY2(fd >= 0, setupError_.c_str());
    Config cfg;
    Committer committer(cfg, serverPath_);
    ReplyResult reply;
    std::thread server([fd, &reply] { recvAndReply(fd, "OK\n", &reply); });

    auto [ok, detail] = committer.commitText("hello");
    server.join();
    close(fd);
    unlink(serverPath_.c_str());

    QVERIFY2(reply.received > 0, std::string("server recv errno=" + std::to_string(reply.receiveErrno)).c_str());
    QCOMPARE(QString::fromStdString(reply.payload), QStringLiteral("COMMIT\nhello"));
    QVERIFY2(reply.sent > 0, std::string("server send errno=" + std::to_string(reply.sendErrno)).c_str());
    QVERIFY2(ok, detail.c_str());
    QCOMPARE(QString::fromStdString(detail), QStringLiteral("OK"));
}

void TestCommitter::commitReturnsErrOnNack()
{
    int fd = setupServer();
    QVERIFY2(fd >= 0, setupError_.c_str());
    Config cfg;
    Committer committer(cfg, serverPath_);
    ReplyResult reply;
    std::thread server([fd, &reply] { recvAndReply(fd, "NO\n", &reply); });

    auto [ok, detail] = committer.commitText("hello");
    server.join();
    close(fd);
    unlink(serverPath_.c_str());

    QVERIFY2(reply.received > 0, std::string("server recv errno=" + std::to_string(reply.receiveErrno)).c_str());
    QCOMPARE(QString::fromStdString(reply.payload), QStringLiteral("COMMIT\nhello"));
    QVERIFY2(reply.sent > 0, std::string("server send errno=" + std::to_string(reply.sendErrno)).c_str());
    QVERIFY(!ok);
    QCOMPARE(QString::fromStdString(detail), QStringLiteral("NO"));
}

QTEST_GUILESS_MAIN(TestCommitter)
#include "test_committer.moc"
