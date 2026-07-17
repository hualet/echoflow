// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SetupCommandRunner.h"

#include <QProcess>
#include <QTimer>

namespace {

constexpr int kDefaultTimeoutMs = 10000;
constexpr int kTerminateGraceMs = 50;

} // namespace

QProcessSetupCommandRunner::QProcessSetupCommandRunner(QObject *parent)
    : QProcessSetupCommandRunner(kDefaultTimeoutMs, parent)
{
}

QProcessSetupCommandRunner::QProcessSetupCommandRunner(int timeoutMs,
                                                       QObject *parent)
    : SetupCommandRunner(parent)
    , timeoutMs_(qMax(1, timeoutMs))
{
}

void QProcessSetupCommandRunner::run(const QString &id, const QString &program,
                                     const QStringList &arguments)
{
    if (processes_.contains(id)) {
        emit finished(id, false,
                      QStringLiteral("Command id '%1' is already running").arg(id));
        return;
    }

    auto *process = new QProcess(this);
    auto *deadline = new QTimer(process);
    deadline->setSingleShot(true);
    processes_.insert(id, {process, deadline});

    connect(deadline, &QTimer::timeout, this, [this, id, process] {
        auto command = processes_.find(id);
        if (command == processes_.end() || command->process != process) {
            return;
        }
        command->timedOut = true;
        process->terminate();
        QTimer::singleShot(kTerminateGraceMs, process, [process] {
            if (process->state() != QProcess::NotRunning) {
                process->kill();
            }
        });
    });

    connect(process, &QProcess::errorOccurred, this,
            [this, id, program, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) {
            return;
        }
        QString detail = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
        if (detail.isEmpty()) {
            detail = QStringLiteral("%1 failed to start: %2")
                         .arg(program, process->errorString());
        }
        finishProcess(id, process, false, detail);
    });
    connect(process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, id, program, process](int exitCode, QProcess::ExitStatus status) {
        const auto command = processes_.constFind(id);
        if (command == processes_.cend() || command->process != process) {
            return;
        }
        if (command->timedOut) {
            finishProcess(id, process, false,
                          QStringLiteral("%1 timed out after %2 ms")
                              .arg(program)
                              .arg(timeoutMs_));
            return;
        }
        QString detail = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
        if (status == QProcess::NormalExit && exitCode == 0) {
            finishProcess(id, process, true, {});
            return;
        }
        if (detail.isEmpty()) {
            detail = QString::fromLocal8Bit(process->readAllStandardOutput()).trimmed();
        }
        if (detail.isEmpty()) {
            detail = status == QProcess::CrashExit
                ? QStringLiteral("%1 crashed").arg(program)
                : QStringLiteral("%1 exited with status %2").arg(program).arg(exitCode);
        }
        finishProcess(id, process, false, detail);
    });

    process->start(program, arguments);
    deadline->start(timeoutMs_);
}

void QProcessSetupCommandRunner::finishProcess(const QString &id,
                                               QProcess *process, bool ok,
                                               const QString &error)
{
    auto command = processes_.find(id);
    if (command == processes_.end() || command->process != process) {
        return;
    }
    command->deadline->stop();
    processes_.erase(command);
    emit finished(id, ok, error);
    process->deleteLater();
}
