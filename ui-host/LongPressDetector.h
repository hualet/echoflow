// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>

#include <QObject>
#include <QPoint>
#include <QRect>
#include <QTimer>

// Turns global left-button presses into press-and-hold voice sessions. A
// press only qualifies when it lands in the active window, on or below the
// line of the caret Fcitx reports for the focused text field (the empty area
// below the last line is part of the field). All coordinates are logical.
class LongPressDetector final : public QObject {
    Q_OBJECT

public:
    struct Options {
        int holdMs = 500;
        // Pointer travel that turns a hold into a click or text selection.
        int moveTolerance = 6;
        // How far below the caret line a press may land, covering the empty
        // area of a multi-line field such as a chat input box.
        int belowCaretReach = 400;
        int pollMs = 30;
    };

    using CursorProvider = std::function<QPoint()>;
    using CanStart = std::function<bool()>;
    // Whether the pointer is over the active window, which keeps dock and
    // other non-focusable surfaces from triggering.
    using PointerInActiveWindow = std::function<bool()>;

    LongPressDetector(CursorProvider cursor, CanStart canStart,
                      QObject *parent = nullptr);
    LongPressDetector(CursorProvider cursor, CanStart canStart,
                      Options options, QObject *parent = nullptr);

    void setPointerInActiveWindow(PointerInActiveWindow check);
    bool enabled() const;
    bool holding() const;

public slots:
    void setEnabled(bool enabled);
    void handlePress(QPoint position);
    void handleRelease(QPoint position);
    void handleInputFocus(bool focused, QRect caret);

signals:
    void holdStarted();
    void holdFinished();
    void holdCancelled();

private:
    enum class State {
        Idle,
        Pending,
        Holding,
    };

    bool movedFromPress(const QPoint &position) const;
    bool pressLandedInText() const;
    void pollCursor();
    void holdElapsed();
    void reset();

    CursorProvider cursor_;
    CanStart canStart_;
    Options options_;
    bool enabled_ = true;
    State state_ = State::Idle;
    QPoint pressPosition_;
    PointerInActiveWindow pointerInActiveWindow_;

    bool focused_ = false;
    QRect caret_;

    QTimer holdTimer_;
    QTimer pollTimer_;
};
