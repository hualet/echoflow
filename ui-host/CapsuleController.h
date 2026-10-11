// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <string_view>

#include <QElapsedTimer>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QTimer>

class CapsuleController final : public QObject {
    Q_OBJECT

public:
    using SendCommand = std::function<bool(std::string_view)>;

    static constexpr int kCapsuleHeight = 40;
    static constexpr int kHintHeight = 28;
    static constexpr int kCaretGap = 6;
    static constexpr int kDefaultHintTimeoutMs = 3000;
    static constexpr int kHintSettleMs = 30;
    // A focus report this close to a left press is taken as caused by it.
    static constexpr int kClickFocusWindowMs = 400;

    explicit CapsuleController(SendCommand sendCommand, QObject *parent = nullptr);

    bool persistent() const;
    bool idle() const;
    void setHintTimeout(int milliseconds);

public slots:
    void setPersistent(bool persistent);
    void setFocusHintEnabled(bool enabled);
    void setLongPressHint(bool longPressAvailable);
    bool applyServiceMessage(QString message);
    // position is logical; used to anchor click-focused fields.
    void notePointerPress(QPoint position);
    void requestToggle();
    void requestCancel();

signals:
    // (moveX, moveY) is the bottom-center anchor, or the top-left anchor when
    // anchorTopLeft is set for a capsule placed beside the caret.
    void capsuleChanged(bool visible, QString phase, QString message,
                        bool hasPosition, int moveX, int moveY,
                        bool anchorTopLeft);
    void hintChanged(bool visible, QString text, int moveX, int moveY);
    void persistentChanged(bool persistent);
    // caret is in logical coordinates and null when unknown.
    void inputFocusChanged(bool focused, QRect caret);

private:
    enum class Phase {
        Idle,
        Recording,
        Transcribing,
        Cancelling,
    };

    static QString phaseName(Phase phase);
    static QPoint placeBesideCaret(const QRect &caret, int height);
    bool applyFocusMessage(const QString &verb, const QString &geometry);
    void publish();
    void updateHint();
    void publishHint();
    bool recentPress() const;
    QRect caretAtPress();

    SendCommand sendCommand_;
    Phase phase_ = Phase::Idle;
    QString message_;
    bool persistent_ = false;

    bool focused_ = false;
    // caret_ is the effective anchor; reportedCaret_ is what Fcitx reported.
    QRect caret_;
    QRect reportedCaret_;
    bool anchoredAtPress_ = false;
    QRect capsuleAnchor_;
    QElapsedTimer clock_;
    QPoint lastPress_;
    qint64 lastPressAt_ = -1;
    qint64 lastFocusAt_ = -1;

    bool focusHintEnabled_ = true;
    bool longPressHint_ = false;
    // Set on focus-in and cleared once the hint has had its turn, so caret
    // moves never bring back a hint that timed out or was typed over.
    bool hintRequested_ = false;
    bool hintVisible_ = false;
    QString hintText_;
    QPoint hintPosition_;
    QTimer hintTimer_;
    QTimer hintPublishTimer_;
};
