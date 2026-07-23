// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CapsuleController.h"

#include <utility>

#include <QGuiApplication>
#include <QRect>
#include <QScreen>

QString CapsuleController::phaseName(Phase phase) {
    switch (phase) {
    case CapsuleController::Phase::Idle:
        return QStringLiteral("idle");
    case CapsuleController::Phase::Recording:
        return QStringLiteral("recording");
    case CapsuleController::Phase::Transcribing:
        return QStringLiteral("transcribing");
    case CapsuleController::Phase::Cancelling:
        return QStringLiteral("cancelling");
    }
    return {};
}

CapsuleController::CapsuleController(SendCommand sendCommand, QObject *parent)
    : QObject(parent)
    , sendCommand_(std::move(sendCommand)) {}

bool CapsuleController::persistent() const {
    return persistent_;
}

void CapsuleController::setPersistent(bool persistent) {
    if (persistent_ == persistent) {
        return;
    }

    persistent_ = persistent;
    emit persistentChanged(persistent_);

    if (persistent_) {
        publish();
        sendCommand_("STATUS");
        return;
    }

    phase_ = Phase::Cancelling;
    message_ = QStringLiteral("正在取消");
    publish();
    sendCommand_("CANCEL");
}

void CapsuleController::applyServiceMessage(QString message) {
    if (message == QStringLiteral("RECORDING")) {
        phase_ = Phase::Recording;
        message_ = QStringLiteral("正在聆听");
    } else if (message.startsWith(QStringLiteral("STREAM_TEXT "))) {
        const QString streamText = message.sliced(12);
        if (phase_ != Phase::Recording || streamText.isEmpty()) {
            return;
        }
        message_ = streamText;
    } else if (message == QStringLiteral("TRANSCRIBING")) {
        phase_ = Phase::Transcribing;
        message_ = QStringLiteral("正在转写");
    } else if (message == QStringLiteral("CANCELLING")) {
        phase_ = Phase::Cancelling;
        message_ = QStringLiteral("正在取消");
    } else if (message == QStringLiteral("IDLE")
               || message == QStringLiteral("HIDE_TOOLTIP")) {
        phase_ = Phase::Idle;
        message_.clear();
    } else {
        return;
    }

    publish();
}

void CapsuleController::requestToggle() {
    sendCommand_("CTRL_DOWN");
}

void CapsuleController::requestCancel() {
    sendCommand_("CANCEL");
}

void CapsuleController::publish() {
    const bool visible = persistent_
        || phase_ == Phase::Recording
        || phase_ == Phase::Transcribing;

    bool hasPosition = false;
    int moveX = 0;
    int moveY = 0;
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect available = screen->availableGeometry();
        hasPosition = true;
        moveX = available.left() + available.width() / 2;
        moveY = available.bottom() - 8;
    }

    emit capsuleChanged(visible, phaseName(phase_), message_,
                        hasPosition, moveX, moveY);
}
