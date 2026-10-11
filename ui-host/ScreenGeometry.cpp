// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ScreenGeometry.h"

#include <QGuiApplication>
#include <QScreen>
#include <QtMath>

namespace echoflow {

namespace {

QScreen *screenForNative(const QPoint &native) {
    for (QScreen *screen : QGuiApplication::screens()) {
        const QRect logical = screen->geometry();
        const qreal ratio = screen->devicePixelRatio();
        const QRect nativeRect(logical.topLeft(),
                               QSize(qCeil(logical.width() * ratio),
                                     qCeil(logical.height() * ratio)));
        if (nativeRect.contains(native)) {
            return screen;
        }
    }
    return QGuiApplication::primaryScreen();
}

} // namespace

QPoint nativeToLogical(const QPoint &native) {
    QScreen *screen = screenForNative(native);
    if (!screen) {
        return native;
    }
    const qreal ratio = screen->devicePixelRatio();
    const QPoint origin = screen->geometry().topLeft();
    return origin + QPoint(qRound((native.x() - origin.x()) / ratio),
                           qRound((native.y() - origin.y()) / ratio));
}

QRect nativeToLogical(const QRect &native) {
    QScreen *screen = screenForNative(native.topLeft());
    const qreal ratio = screen ? screen->devicePixelRatio() : 1.0;
    return QRect(nativeToLogical(native.topLeft()),
                 QSize(qRound(native.width() / ratio),
                       qMax(1, qRound(native.height() / ratio))));
}

} // namespace echoflow
