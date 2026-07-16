// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SetupCommandRunner.h"

#include <QProcess>

void QProcessSetupCommandRunner::run(const QString &id, const QString &program,
                                     const QStringList &arguments)
{
    if (processes_.contains(id)) {
        emit finished(id, false,
                      QStringLiteral("Command id '%1' is already running").arg(id));
        return;
    }

    auto *process = new QProcess(this);
    processes_.insert(id, process);

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
        QString detail = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
        if (status == QProcess::NormalExit && exitCode == 0) {
            finishProcess(id, process, true, {});
            return;
        }
        if (detail.isEmpty()) {
            detail = status == QProcess::CrashExit
                ? QStringLiteral("%1 crashed").arg(program)
                : QStringLiteral("%1 exited with status %2").arg(program).arg(exitCode);
        }
        finishProcess(id, process, false, detail);
    });

    process->start(program, arguments);
}

void QProcessSetupCommandRunner::finishProcess(const QString &id,
                                               QProcess *process, bool ok,
                                               const QString &error)
{
    if (processes_.value(id) != process) {
        return;
    }
    processes_.remove(id);
    emit finished(id, ok, error);
    process->deleteLater();
}
