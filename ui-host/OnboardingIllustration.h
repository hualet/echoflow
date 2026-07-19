// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QLabel>
#include <QPixmap>

class OnboardingIllustration final : public QLabel {
public:
    explicit OnboardingIllustration(const QString &resourcePath,
                                    const QString &accessibleName,
                                    QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QPixmap source_;
};
