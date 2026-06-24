// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_CRISP_ASR_ENGINE_H
#define ECHOFLOW_CRISP_ASR_ENGINE_H

#include "Config.h"
#include "Interfaces.h"

#include <filesystem>
#include <string>
#include <vector>

namespace echoflow {

// One-shot file transcription via the external `crispasr` binary.
// Used by the press-to-talk path and --transcribe-file.
class CrispAsrEngine : public IAsrEngine {
public:
    explicit CrispAsrEngine(Config cfg);
    std::string transcribe(const std::filesystem::path& audio) override;

    static std::vector<std::string> buildArgs(const Config& cfg,
                                              const std::filesystem::path& audio);

private:
    Config cfg_;
};

}  // namespace echoflow

#endif
