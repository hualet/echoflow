// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SettingsDialog.h"

#include <DSettings>

namespace echoflow {

SettingsDialog::SettingsDialog(Dtk::Core::DSettings *settings, QWidget *parent)
    : Dtk::Widget::DSettingsDialog(parent) {
    setWindowTitle(tr("EchoFlow 设置"));
    updateSettings(settings);
}

} // namespace echoflow
