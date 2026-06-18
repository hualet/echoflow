// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_UI_NOTIFIER_H
#define ECHOFLOW_UI_NOTIFIER_H

#include "Interfaces.h"

#include <filesystem>

namespace echoflow {

class UnixDatagramUiNotifier : public IUiNotifier {
public:
    explicit UnixDatagramUiNotifier(std::filesystem::path socket);
    void send(const std::string& message) override;

private:
    std::filesystem::path socket_;
};

}  // namespace echoflow

#endif
