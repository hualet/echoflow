// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SileroVadBackend.h"

extern "C" {
#include "crispasr_session.h"
}

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace echoflow {

SileroVadBackend::SileroVadBackend(std::filesystem::path modelPath, float threshold)
    : modelPath_(std::move(modelPath))
    , threshold_(threshold)
{
}

std::vector<SpeechInterval> SileroVadBackend::detect(const int16_t* pcm, size_t count)
{
    if (!std::filesystem::is_regular_file(modelPath_)) {
        throw std::runtime_error("missing Silero VAD model: " + modelPath_.string());
    }
    if (!pcm || count == 0) return {};

    std::vector<float> audio;
    audio.reserve(count);
    for (size_t i = 0; i < count; ++i) audio.push_back(pcm[i] / 32768.0f);

    float* spans = nullptr;
    const int detected = crispasr_vad_segments(
        modelPath_.c_str(), audio.data(), static_cast<int>(audio.size()), 16000,
        threshold_, 100, 300, 2, false, &spans);
    if (detected < 0) {
        throw std::runtime_error("failed to run Silero VAD model: " + modelPath_.string());
    }

    std::vector<SpeechInterval> intervals;
    intervals.reserve(static_cast<size_t>(detected));
    for (int i = 0; i < detected; ++i) {
        const double beginSeconds = spans[2 * i] / 100.0;
        const double endSeconds = spans[2 * i + 1] / 100.0;
        const size_t begin = std::min(count, static_cast<size_t>(beginSeconds * 16000.0));
        const size_t end = std::min(count, static_cast<size_t>(endSeconds * 16000.0));
        if (end > begin) intervals.push_back({begin, end, 1.0f});
    }
    crispasr_vad_free(spans);
    return intervals;
}

std::string SileroVadBackend::name() const
{
    return "silero";
}

}  // namespace echoflow
