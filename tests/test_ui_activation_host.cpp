// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "UiActivationHost.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>

#include <chrono>
#include <thread>

namespace {

class ScopedThread {
public:
    explicit ScopedThread(QThread *thread)
        : thread_(thread)
    {
    }

    ~ScopedThread()
    {
        thread_->wait();
        delete thread_;
    }

    void start() { thread_->start(); }
    bool wait(unsigned long timeoutMs) { return thread_->wait(timeoutMs); }

    ScopedThread(const ScopedThread &) = delete;
    ScopedThread &operator=(const ScopedThread &) = delete;

private:
    QThread *thread_ = nullptr;
};

} // namespace

class TestUiActivationHost : public QObject {
    Q_OBJECT

private slots:
    void servesActivationWhileOwnerThreadIsBlocked();
    void rejectsRepeatedAcquire();
    void cleanupPathsJoinWorker();
};

void TestUiActivationHost::servesActivationWhileOwnerThreadIsBlocked()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ui.sock"));
    UiActivationHost primary(path);
    QString primaryError;
    QCOMPARE(primary.acquire(false, &primaryError),
             UiActivationServer::Result::Primary);
    QVERIFY2(primaryError.isEmpty(), qPrintable(primaryError));
    QSignalSpy activationSpy(&primary, &UiActivationHost::activateRequested);

    UiActivationServer::Result secondaryResult =
        UiActivationServer::Result::Failed;
    QString secondaryError;
    ScopedThread secondary(QThread::create([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        UiActivationServer client(path);
        secondaryResult = client.acquire(true, &secondaryError);
    }));
    secondary.start();

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    QVERIFY(secondary.wait(500));

    QCOMPARE(secondaryResult, UiActivationServer::Result::ActivatedExisting);
    QVERIFY2(secondaryError.isEmpty(), qPrintable(secondaryError));
    QCOMPARE(activationSpy.count(), 0);
    QTRY_COMPARE(activationSpy.count(), 1);
}

void TestUiActivationHost::rejectsRepeatedAcquire()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    UiActivationHost host(dir.filePath(QStringLiteral("ui.sock")));
    QCOMPARE(host.acquire(false), UiActivationServer::Result::Primary);

    QString error;
    QCOMPARE(host.acquire(false, &error), UiActivationServer::Result::Failed);
    QVERIFY2(error.contains(QStringLiteral("once"), Qt::CaseInsensitive),
             qPrintable(error));
}

void TestUiActivationHost::cleanupPathsJoinWorker()
{
    for (int i = 0; i < 10; ++i) {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("ui.sock"));
        UiActivationHost primary(path);
        QCOMPARE(primary.acquire(false), UiActivationServer::Result::Primary);

        UiActivationHost secondary(path);
        QCOMPARE(secondary.acquire(false),
                 UiActivationServer::Result::ActivatedExisting);

        UiActivationHost failed(
            dir.filePath(QStringLiteral("missing/ui.sock")));
        QString error;
        QCOMPARE(failed.acquire(false, &error),
                 UiActivationServer::Result::Failed);
        QVERIFY2(!error.isEmpty(), qPrintable(error));
    }
}

QTEST_GUILESS_MAIN(TestUiActivationHost)
#include "test_ui_activation_host.moc"
