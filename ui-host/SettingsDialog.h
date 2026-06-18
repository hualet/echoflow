// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <DSettingsDialog>

namespace Dtk {
namespace Core {
class DSettings;
}
} // namespace Dtk

namespace echoflow {

class SettingsDialog : public Dtk::Widget::DSettingsDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(Dtk::Core::DSettings *settings, QWidget *parent = nullptr);
};

} // namespace echoflow
