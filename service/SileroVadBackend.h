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
                              float startThreshold = 0.5f,
                              float endThreshold = 0.35f,
                              int minSpeechMs = 100,
                              int minSilenceMs = 300);
    ~SileroVadBackend() override;

    SileroVadBackend(const SileroVadBackend&) = delete;
    SileroVadBackend& operator=(const SileroVadBackend&) = delete;

    std::vector<SpeechInterval> detect(const int16_t* pcm, size_t count) override;
    std::string name() const override;

private:
    std::filesystem::path modelPath_;
    float startThreshold_;
    float endThreshold_;
    int minSpeechMs_;
    int minSilenceMs_;
    int nWindow_ = 0;
    whisper_vad_context* context_ = nullptr;
};

// Dual-threshold speech segmentation over raw Silero VAD probabilities.
//
// probs[i] is the speech probability for the window
// [i * nWindow, min((i + 1) * nWindow, totalSamples)). A frame with
// probability >= startThreshold opens a speech segment; once open, a run of
// frames below endThreshold that lasts at least minSilenceMs closes it. The
// closing frame's start sample becomes the segment end, matching the contract
// that a still-open utterance is returned with end == totalSamples. Closed
// segments shorter than minSpeechMs are dropped.
//
// Exposed for deterministic unit testing without model weights.
std::vector<SpeechInterval> segmentByDualThreshold(const float* probs, int nProbs,
                                                   int nWindow, size_t totalSamples,
                                                   float startThreshold, float endThreshold,
                                                   int minSpeechMs, int minSilenceMs);

}  // namespace echoflow

#endif
