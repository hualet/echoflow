// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;

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
    using SetupCommandRunner::SetupCommandRunner;

    void run(const QString &id, const QString &program,
             const QStringList &arguments) override;

private:
    void finishProcess(const QString &id, QProcess *process, bool ok,
                       const QString &error);

    QHash<QString, QProcess *> processes_;
};
