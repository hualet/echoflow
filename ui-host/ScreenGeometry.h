// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QPoint>
#include <QRect>

namespace echoflow {

// Fcitx caret rectangles and XEventMonitor button events use native X11
// pixels, while Qt windows are positioned in logical coordinates. Qt keeps
// each screen's origin unscaled and scales offsets within the screen.
QPoint nativeToLogical(const QPoint &native);
QRect nativeToLogical(const QRect &native);

// Caret rectangles are often zero pixels wide, so QRect::isValid() would
// reject them; a positive height is what marks a known caret.
inline bool hasCaret(const QRect &caret) {
    return caret.height() > 0;
}

} // namespace echoflow
