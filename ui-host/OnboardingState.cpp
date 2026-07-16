// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "OnboardingState.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include <utility>

namespace {

constexpr auto kVersionKey = "onboarding/version";

QString settingsStatusName(QSettings::Status status)
{
    switch (status) {
    case QSettings::NoError:
        return QStringLiteral("NoError");
    case QSettings::AccessError:
        return QStringLiteral("AccessError");
    case QSettings::FormatError:
        return QStringLiteral("FormatError");
    }
    return QStringLiteral("UnknownError");
}

} // namespace

OnboardingState::OnboardingState(QString path)
    : path_(std::move(path))
{
}

bool OnboardingState::isComplete() const
{
    QSettings settings(path_, QSettings::IniFormat);
    return settings.value(QString::fromLatin1(kVersionKey), 0).toInt()
        >= kCurrentVersion;
}

bool OnboardingState::markComplete(QString *error)
{
    if (error) {
        error->clear();
    }

    QDir parentDirectory = QFileInfo(path_).dir();
    if (!parentDirectory.mkpath(QStringLiteral("."))) {
        if (error) {
            *error = QStringLiteral("Failed to create onboarding state directory: %1")
                         .arg(parentDirectory.absolutePath());
        }
        return false;
    }

    QSettings settings(path_, QSettings::IniFormat);
    settings.setValue(QString::fromLatin1(kVersionKey), kCurrentVersion);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        if (error) {
            *error = QStringLiteral("Failed to write onboarding state %1 (%2)")
                         .arg(path_, settingsStatusName(settings.status()));
        }
        return false;
    }

    return true;
}

QString OnboardingState::path() const
{
    return path_;
}

QString OnboardingState::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
        + QStringLiteral("/echoflow/ui-state.ini");
}
