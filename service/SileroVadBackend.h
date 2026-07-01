// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_SILERO_VAD_BACKEND_H
#define ECHOFLOW_SILERO_VAD_BACKEND_H

#include "VadBackend.h"

#include <filesystem>

namespace echoflow {

class SileroVadBackend : public IVadBackend {
public:
    explicit SileroVadBackend(std::filesystem::path modelPath, float threshold = 0.5f);

    std::vector<SpeechInterval> detect(const int16_t* pcm, size_t count) override;
    std::string name() const override;

private:
    std::filesystem::path modelPath_;
    float threshold_ = 0.5f;
};

}  // namespace echoflow

#endif
