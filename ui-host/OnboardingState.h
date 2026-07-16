// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

class OnboardingState {
public:
    static constexpr int kCurrentVersion = 1;

    explicit OnboardingState(QString path = defaultPath());

    bool isComplete() const;
    bool markComplete(QString *error = nullptr);
    QString path() const;

    static QString defaultPath();

private:
    QString path_;
};
