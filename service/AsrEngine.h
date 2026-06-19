// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_ASR_ENGINE_H
#define ECHOFLOW_ASR_ENGINE_H

#include "Config.h"
#include "Interfaces.h"

extern "C" {
#include "qwen_asr.h"
}

#include <filesystem>

namespace echoflow {

class AsrEngine : public IAsrEngine {
public:
    explicit AsrEngine(Config cfg);
    ~AsrEngine() override;

    AsrEngine(const AsrEngine&) = delete;
    AsrEngine& operator=(const AsrEngine&) = delete;

    bool preload();
    std::string transcribe(const std::filesystem::path& audio) override;

private:
    bool ensureLoaded();

    Config cfg_;
    qwen_ctx_t* ctx_ = nullptr;
};

}  // namespace echoflow

#endif
