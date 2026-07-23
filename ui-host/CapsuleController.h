// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <string_view>

#include <QObject>
#include <QString>

class CapsuleController final : public QObject {
    Q_OBJECT

public:
    using SendCommand = std::function<bool(std::string_view)>;

    explicit CapsuleController(SendCommand sendCommand, QObject *parent = nullptr);

    bool persistent() const;

public slots:
    void setPersistent(bool persistent);
    void applyServiceMessage(QString message);
    void requestToggle();
    void requestCancel();

signals:
    void capsuleChanged(bool visible, QString phase, QString message,
                        bool hasPosition, int moveX, int moveY);
    void persistentChanged(bool persistent);

private:
    enum class Phase {
        Idle,
        Recording,
        Transcribing,
        Cancelling,
    };

    static QString phaseName(Phase phase);
    void publish();

    SendCommand sendCommand_;
    Phase phase_ = Phase::Idle;
    QString message_;
    bool persistent_ = false;
};
