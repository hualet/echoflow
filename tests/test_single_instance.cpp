// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "SingleInstance.h"

#include <cstring>
#include <filesystem>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using namespace echoflow;

namespace {

std::filesystem::path testSocketPath(const QTemporaryDir &dir, const char *name)
{
    return std::filesystem::path(dir.path().toStdString()) / name;
}

int bindDatagramSocket(const std::filesystem::path &path)
{
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }

    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

} // namespace

class TestSingleInstance : public QObject {
    Q_OBJECT

private slots:
    void refusesSecondLiveServiceInstance();
    void replacesStaleServiceSocket();
};

void TestSingleInstance::refusesSecondLiveServiceInstance()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto socketPath = testSocketPath(dir, "echoflow-control.sock");

    SingleInstanceGuard first;
    std::string firstError;
    QVERIFY2(first.acquire(socketPath, &firstError), firstError.c_str());
    QVERIFY(first.isAcquired());
    QVERIFY(first.fd() >= 0);

    SingleInstanceGuard second;
    std::string secondError;
    QVERIFY(!second.acquire(socketPath, &secondError));
    QVERIFY(secondError.find("already running") != std::string::npos);
    QVERIFY(!second.isAcquired());
}

void TestSingleInstance::replacesStaleServiceSocket()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto socketPath = testSocketPath(dir, "echoflow-control.sock");

    int staleFd = bindDatagramSocket(socketPath);
    QVERIFY(staleFd >= 0);
    close(staleFd);
    QVERIFY(std::filesystem::exists(socketPath));

    SingleInstanceGuard guard;
    std::string error;
    QVERIFY2(guard.acquire(socketPath, &error), error.c_str());
    QVERIFY(guard.isAcquired());
    QVERIFY(guard.fd() >= 0);
}

QTEST_GUILESS_MAIN(TestSingleInstance)
#include "test_single_instance.moc"
