// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "OnboardingIllustration.h"

#include <QPainter>
#include <QPixmap>
#include <QSizePolicy>

OnboardingIllustration::OnboardingIllustration(
    const QString &resourcePath, const QString &accessibleName, QWidget *parent)
    : QLabel(parent)
{
    const QPixmap source(resourcePath);
    if (source.isNull()) {
        clear();
        setAccessibleName({});
        setMinimumSize(0, 0);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        hide();
        return;
    }

    setAccessibleName(accessibleName);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setPixmap(source);
}

QSize OnboardingIllustration::sizeHint() const
{
    return pixmap(Qt::ReturnByValue).isNull() ? QSize(0, 0)
                                              : QSize(260, 300);
}

QSize OnboardingIllustration::minimumSizeHint() const
{
    return pixmap(Qt::ReturnByValue).isNull() ? QSize(0, 0)
                                              : QSize(260, 300);
}

void OnboardingIllustration::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    const QPixmap current = pixmap(Qt::ReturnByValue);
    if (current.isNull()) {
        return;
    }

    const QRect bounds = contentsRect();
    QSize fitted = current.size();
    fitted.scale(bounds.size(), Qt::KeepAspectRatio);
    QRect target(QPoint(), fitted);
    target.moveCenter(bounds.center());

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawPixmap(target, current, current.rect());
}
