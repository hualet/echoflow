// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_CRISP_ASR_ENGINE_H
#define ECHOFLOW_CRISP_ASR_ENGINE_H

#include "Config.h"
#include "Interfaces.h"

#include <filesystem>
#include <memory>
#include <string>

namespace echoflow {

class CrispSession;

class CrispAsrEngine : public IAsrEngine {
public:
    explicit CrispAsrEngine(Config cfg);
    ~CrispAsrEngine() override;
    std::string transcribe(const std::filesystem::path& audio) override;

    static std::string languageCode(const std::string& value);

private:
    Config cfg_;
    std::unique_ptr<CrispSession> session_;
};

}  // namespace echoflow

#endif
