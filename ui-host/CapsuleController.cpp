// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CapsuleController.h"

#include "ScreenGeometry.h"

#include <utility>

#include <QGuiApplication>
#include <QRect>
#include <QScreen>
#include <QStringList>

using echoflow::hasCaret;

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
    , sendCommand_(std::move(sendCommand)) {
    clock_.start();
    hintTimer_.setSingleShot(true);
    hintTimer_.setInterval(kDefaultHintTimeoutMs);
    // Chromium blurs and refocuses its input context several times per
    // click; coalescing keeps the hint from flickering through that churn.
    hintPublishTimer_.setSingleShot(true);
    hintPublishTimer_.setInterval(kHintSettleMs);
    connect(&hintPublishTimer_, &QTimer::timeout, this, &CapsuleController::publishHint);
    connect(&hintTimer_, &QTimer::timeout, this, [this] {
        hintRequested_ = false;
        updateHint();
    });
}

bool CapsuleController::persistent() const {
    return persistent_;
}

bool CapsuleController::idle() const {
    return phase_ == Phase::Idle;
}

void CapsuleController::setHintTimeout(int milliseconds) {
    hintTimer_.setInterval(milliseconds);
}

void CapsuleController::setPersistent(bool persistent) {
    if (persistent_ == persistent) {
        return;
    }

    persistent_ = persistent;
    emit persistentChanged(persistent_);
    updateHint();

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

void CapsuleController::setFocusHintEnabled(bool enabled) {
    focusHintEnabled_ = enabled;
    updateHint();
}

void CapsuleController::setLongPressHint(bool longPressAvailable) {
    longPressHint_ = longPressAvailable;
    updateHint();
}

bool CapsuleController::applyServiceMessage(QString message) {
    const qsizetype separator = message.indexOf(QLatin1Char(' '));
    const QString verb = separator < 0 ? message : message.first(separator);
    if (verb == QStringLiteral("FOCUS") || verb == QStringLiteral("CARET")
        || verb == QStringLiteral("TYPED") || verb == QStringLiteral("BLUR")) {
        return applyFocusMessage(verb, separator < 0 ? QString() : message.sliced(separator + 1));
    }

    if (message == QStringLiteral("RECORDING")) {
        phase_ = Phase::Recording;
        message_ = QStringLiteral("正在聆听");
    } else if (message.startsWith(QStringLiteral("STREAM_TEXT "))) {
        const QString streamText = message.sliced(12);
        if (phase_ != Phase::Recording || streamText.isEmpty()) {
            return false;
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
        return false;
    }

    if (phase_ != Phase::Idle) {
        hintRequested_ = false;
    }

    updateHint();
    publish();
    return true;
}

bool CapsuleController::applyFocusMessage(const QString &verb, const QString &geometry) {
    if (verb == QStringLiteral("TYPED")) {
        hintRequested_ = false;
        updateHint();
        return true;
    }

    if (verb == QStringLiteral("BLUR")) {
        focused_ = false;
        caret_ = QRect();
        reportedCaret_ = QRect();
        anchoredAtPress_ = false;
        hintRequested_ = false;
        updateHint();
        emit inputFocusChanged(false, QRect());
        return true;
    }

    QRect caret;
    const QStringList fields = geometry.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (fields.size() == 4) {
        bool ok[4] = {};
        const int x = fields.at(0).toInt(&ok[0]);
        const int y = fields.at(1).toInt(&ok[1]);
        const int width = fields.at(2).toInt(&ok[2]);
        const int height = fields.at(3).toInt(&ok[3]);
        if (ok[0] && ok[1] && ok[2] && ok[3] && width >= 0 && height > 0) {
            caret = echoflow::nativeToLogical(QRect(x, y, width, height));
        }
    }

    if (verb == QStringLiteral("FOCUS")) {
        focused_ = true;
        hintRequested_ = true;
        hintTimer_.stop();
        reportedCaret_ = caret;
        lastFocusAt_ = clock_.elapsed();
        caret_ = recentPress() ? caretAtPress() : caret;
    } else if (!focused_) {
        return true;
    } else if (anchoredAtPress_ && caret == reportedCaret_) {
        // Chromium repeats its stale focus-time caret even after reporting
        // the real one; keep the press anchor until a new position arrives.
        return true;
    } else {
        reportedCaret_ = caret;
        caret_ = caret;
        anchoredAtPress_ = false;
    }
    updateHint();
    emit inputFocusChanged(true, caret_);
    return true;
}

void CapsuleController::notePointerPress(QPoint position) {
    lastPress_ = position;
    lastPressAt_ = clock_.elapsed();
    // The press can arrive over D-Bus after Fcitx already reported focus.
    if (focused_ && lastFocusAt_ >= 0 && lastPressAt_ - lastFocusAt_ <= kClickFocusWindowMs) {
        caret_ = caretAtPress();
        updateHint();
        emit inputFocusChanged(true, caret_);
    }
}

bool CapsuleController::recentPress() const {
    return lastPressAt_ >= 0 && clock_.elapsed() - lastPressAt_ <= kClickFocusWindowMs;
}

QRect CapsuleController::caretAtPress() {
    if (!hasCaret(reportedCaret_)) {
        // No caret at all (e.g. a password field): nothing to anchor.
        anchoredAtPress_ = false;
        return reportedCaret_;
    }
    anchoredAtPress_ = true;
    const int height = reportedCaret_.height();
    return QRect(lastPress_.x(), lastPress_.y() - height / 2, 0, height);
}

    if (caretNearPress(reportedCaret_)) {
        // The press landed on this caret's line, its field's padding, or the
        // empty space below the last line: it moved the caret itself, so the
        // report is fresher than the raw press point.
        anchoredAtPress_ = false;
        return reportedCaret_;
    }
    // Chromium keeps reporting the previous field's caret when focus moves
    // between its inputs, and only corrects it once the user types.
void CapsuleController::requestToggle() {
    sendCommand_("CTRL_DOWN");
}

void CapsuleController::requestCancel() {
bool CapsuleController::caretNearPress(const QRect &caret) const {
    return caret
        .adjusted(-kCaretNearX, -kCaretNearAbove, kCaretNearX, kCaretNearBelow)
        .contains(lastPress_);
}

    sendCommand_("CANCEL");
}

QPoint CapsuleController::placeBesideCaret(const QRect &caret, int height) {
    QScreen *screen = QGuiApplication::screenAt(caret.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    int x = caret.left();
    int y = caret.bottom() + 1 + kCaretGap;
    if (screen) {
        const QRect available = screen->availableGeometry();
        if (y + height > available.bottom() + 1) {
            y = caret.top() - kCaretGap - height;
        }
        x = qMax(x, available.left());
    }
    return QPoint(x, y);
}

void CapsuleController::updateHint() {
    hintPublishTimer_.start();
}

void CapsuleController::publishHint() {
    const bool visible = hintRequested_ && focusHintEnabled_ && !persistent_
        && phase_ == Phase::Idle && focused_ && hasCaret(caret_);
    const QString text = longPressHint_
        ? QStringLiteral("长按鼠标或右 Ctrl 说话")
        : QStringLiteral("按住右 Ctrl 说话");
    const QPoint position = visible ? placeBesideCaret(caret_, kHintHeight) : hintPosition_;

    if (!visible) {
        hintTimer_.stop();
    } else if (!hintTimer_.isActive()) {
        hintTimer_.start();
    }

    if (visible == hintVisible_ && text == hintText_ && position == hintPosition_) {
        return;
    }
    hintVisible_ = visible;
    hintText_ = text;
    hintPosition_ = position;
    emit hintChanged(hintVisible_, hintText_, hintPosition_.x(), hintPosition_.y());
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
        moveY = available.bottom() - available.height() / kCapsuleBottomDivisor;
    }

    emit capsuleChanged(visible, phaseName(phase_), message_,
                        hasPosition, moveX, moveY);
}
