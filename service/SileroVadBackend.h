// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_SILERO_VAD_BACKEND_H
#define ECHOFLOW_SILERO_VAD_BACKEND_H

#include "VadBackend.h"

#include <filesystem>

struct whisper_vad_context;

namespace echoflow {

class SileroVadBackend final : public IVadBackend {
public:
    explicit SileroVadBackend(std::filesystem::path modelPath,
                              float threshold = 0.5f,
                              int minSpeechMs = 100,
                              int minSilenceMs = 300);
    ~SileroVadBackend() override;

    SileroVadBackend(const SileroVadBackend&) = delete;
    SileroVadBackend& operator=(const SileroVadBackend&) = delete;

    std::vector<SpeechInterval> detect(const int16_t* pcm, size_t count) override;
    std::string name() const override;

private:
    std::filesystem::path modelPath_;
    float threshold_;
    int minSpeechMs_;
    int minSilenceMs_;
    whisper_vad_context* context_ = nullptr;
};

}  // namespace echoflow

#endif
