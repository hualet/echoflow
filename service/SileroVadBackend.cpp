// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SileroVadBackend.h"

extern "C" {
#include "crispasr.h"
}

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

namespace echoflow {

SileroVadBackend::SileroVadBackend(std::filesystem::path modelPath, float threshold,
                                   int minSpeechMs, int minSilenceMs)
    : modelPath_(std::move(modelPath))
    , threshold_(threshold)
    , minSpeechMs_(minSpeechMs)
    , minSilenceMs_(minSilenceMs)
{
    if (!(threshold_ > 0.0f && threshold_ <= 1.0f)) {
        throw std::invalid_argument("Silero VAD threshold must be in (0, 1]");
    }
    if (minSpeechMs_ <= 0 || minSilenceMs_ <= 0) {
        throw std::invalid_argument("Silero VAD durations must be positive");
    }
    if (!std::filesystem::is_regular_file(modelPath_)) {
        throw std::runtime_error("missing Silero VAD model: " + modelPath_.string());
    }

    whisper_vad_context_params params = whisper_vad_default_context_params();
    params.n_threads = 1;
    params.use_gpu = false;
    params.gpu_device = 0;
    context_ = whisper_vad_init_from_file_with_params(modelPath_.c_str(), params);
    if (!context_) {
        throw std::runtime_error("failed to load Silero VAD model: " + modelPath_.string());
    }
}

SileroVadBackend::~SileroVadBackend()
{
    whisper_vad_free(context_);
}

std::vector<SpeechInterval> SileroVadBackend::detect(const int16_t* pcm, size_t count)
{
    if (!pcm || count == 0) return {};

    std::vector<float> audio;
    audio.reserve(count);
    for (size_t i = 0; i < count; ++i) audio.push_back(pcm[i] / 32768.0f);

    if (!whisper_vad_detect_speech(context_, audio.data(), static_cast<int>(audio.size()))) {
        throw std::runtime_error("failed to run Silero VAD model: " + modelPath_.string());
    }

    whisper_vad_params params = whisper_vad_default_params();
    params.threshold = threshold_;
    params.min_speech_duration_ms = minSpeechMs_;
    params.min_silence_duration_ms = minSilenceMs_;
    using SegmentsPtr = std::unique_ptr<whisper_vad_segments,
                                        decltype(&whisper_vad_free_segments)>;
    SegmentsPtr segments(whisper_vad_segments_from_probs(context_, params),
                         &whisper_vad_free_segments);
    if (!segments) {
        throw std::runtime_error("failed to segment Silero VAD probabilities: "
                                 + modelPath_.string());
    }

    std::vector<SpeechInterval> intervals;
    const int detected = whisper_vad_segments_n_segments(segments.get());
    intervals.reserve(static_cast<size_t>(detected));
    for (int i = 0; i < detected; ++i) {
        const double beginSeconds =
            whisper_vad_segments_get_segment_t0(segments.get(), i) / 100.0;
        const double endSeconds =
            whisper_vad_segments_get_segment_t1(segments.get(), i) / 100.0;
        const size_t begin = std::min(count, static_cast<size_t>(beginSeconds * 16000.0));
        const size_t end = std::min(count, static_cast<size_t>(endSeconds * 16000.0));
        if (end > begin) intervals.push_back({begin, end, 1.0f});
    }
    return intervals;
}

std::string SileroVadBackend::name() const
{
    return "silero";
}

}  // namespace echoflow
