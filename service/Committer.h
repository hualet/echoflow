// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_COMMITTER_H
#define ECHOFLOW_COMMITTER_H

#include "Config.h"
#include "Interfaces.h"

namespace echoflow {

class Committer : public ICommitter {
public:
    Committer(const Config& cfg, std::filesystem::path fcitxSocket);
    std::pair<bool, std::string> commitText(const std::string& text) override;

private:
    const Config& cfg_;
    std::filesystem::path fcitxSocket_;
};

}  // namespace echoflow

#endif
