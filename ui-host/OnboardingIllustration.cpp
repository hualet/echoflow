// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "OnboardingIllustration.h"

#include <QPainter>
#include <QSizePolicy>

OnboardingIllustration::OnboardingIllustration(
    const QString &resourcePath, const QString &accessibleName, QWidget *parent)
    : QLabel(parent)
    , source_(resourcePath)
{
    if (source_.isNull()) {
        clear();
        setAccessibleName({});
        setMinimumSize(0, 0);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        hide();
        return;
    }

    setAccessibleName(accessibleName);
    setMinimumSize(260, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setPixmap(source_);
}

QSize OnboardingIllustration::sizeHint() const
{
    return source_.isNull() ? QSize(0, 0) : QSize(260, 300);
}

QSize OnboardingIllustration::minimumSizeHint() const
{
    return source_.isNull() ? QSize(0, 0) : QSize(260, 300);
}

void OnboardingIllustration::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    if (source_.isNull()) {
        return;
    }

    const QRect bounds = contentsRect();
    QSize fitted = source_.size();
    fitted.scale(bounds.size(), Qt::KeepAspectRatio);
    QRect target(QPoint(), fitted);
    target.moveCenter(bounds.center());

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawPixmap(target, source_, source_.rect());
}
