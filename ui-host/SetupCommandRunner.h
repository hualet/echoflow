// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;
class QTimer;

class SetupCommandRunner : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    virtual void run(const QString &id, const QString &program,
                     const QStringList &arguments) = 0;

signals:
    void finished(const QString &id, bool ok, const QString &error);
};

class QProcessSetupCommandRunner final : public SetupCommandRunner {
    Q_OBJECT
public:
    explicit QProcessSetupCommandRunner(QObject *parent = nullptr);
    explicit QProcessSetupCommandRunner(int timeoutMs,
                                        QObject *parent = nullptr);

    void run(const QString &id, const QString &program,
             const QStringList &arguments) override;

private:
    struct RunningCommand {
        QProcess *process;
        QTimer *deadline;
        bool timedOut = false;
    };

    void finishProcess(const QString &id, QProcess *process, bool ok,
                       const QString &error);

    int timeoutMs_;
    QHash<QString, RunningCommand> processes_;
};
