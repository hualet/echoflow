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

namespace {

constexpr int kSampleRate = 16000;

int ceilDiv(int a, int b)
{
    return (a + b - 1) / b;
}

}  // namespace

SileroVadBackend::SileroVadBackend(std::filesystem::path modelPath, float startThreshold,
                                   float endThreshold, int minSpeechMs, int minSilenceMs)
    : modelPath_(std::move(modelPath))
    , startThreshold_(startThreshold)
    , endThreshold_(endThreshold)
    , minSpeechMs_(minSpeechMs)
    , minSilenceMs_(minSilenceMs)
{
    if (!(startThreshold_ > 0.0f && startThreshold_ <= 1.0f)) {
        throw std::invalid_argument("Silero VAD start threshold must be in (0, 1]");
    }
    if (!(endThreshold_ > 0.0f && endThreshold_ <= 1.0f)) {
        throw std::invalid_argument("Silero VAD end threshold must be in (0, 1]");
    }
    if (endThreshold_ > startThreshold_) {
        throw std::invalid_argument(
            "Silero VAD end threshold must not exceed the start threshold");
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

    // Recover the model's per-window sample count without a public getter. The
    // identity ceil(N / ceil(N / W)) == W holds for N >= W, so one inference
    // pass over a full second of audio pins nWindow_ exactly.
    std::vector<float> probe(static_cast<size_t>(kSampleRate), 0.0f);
    if (whisper_vad_detect_speech(context_, probe.data(), kSampleRate)) {
        const int nProbs = whisper_vad_n_probs(context_);
        if (nProbs >= 2) {
            nWindow_ = ceilDiv(kSampleRate, nProbs);
        }
    }
    if (nWindow_ <= 0) {
        whisper_vad_free(context_);
        context_ = nullptr;
        throw std::runtime_error("failed to probe Silero VAD window size: "
                                 + modelPath_.string());
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

    const int nProbs = whisper_vad_n_probs(context_);
    const float* probs = whisper_vad_probs(context_);
    if (nProbs <= 0 || !probs) return {};

    return segmentByDualThreshold(probs, nProbs, nWindow_, count,
                                  startThreshold_, endThreshold_,
                                  minSpeechMs_, minSilenceMs_);
}

std::string SileroVadBackend::name() const
{
    return "silero";
}

std::vector<SpeechInterval> segmentByDualThreshold(const float* probs, int nProbs,
                                                   int nWindow, size_t totalSamples,
                                                   float startThreshold, float endThreshold,
                                                   int minSpeechMs, int minSilenceMs)
{
    std::vector<SpeechInterval> segments;
    if (!probs || nProbs <= 0 || nWindow <= 0 || totalSamples == 0) return segments;

    const size_t minSilenceSamples =
        static_cast<size_t>(static_cast<int64_t>(kSampleRate) * minSilenceMs / 1000);
    const size_t minSpeechSamples =
        static_cast<size_t>(static_cast<int64_t>(kSampleRate) * minSpeechMs / 1000);

    bool inSpeech = false;
    bool trailingSilence = false;
    size_t speechStart = 0;
    size_t silenceStart = 0;

    auto frameStart = [&](int i) {
        return std::min(totalSamples, static_cast<size_t>(i) * static_cast<size_t>(nWindow));
    };
    auto frameEnd = [&](int i) {
        return std::min(totalSamples,
                        (static_cast<size_t>(i) + 1) * static_cast<size_t>(nWindow));
    };

    for (int i = 0; i < nProbs; ++i) {
        const float prob = probs[i];
        const size_t begin = frameStart(i);

        if (!inSpeech) {
            if (prob >= startThreshold) {
                inSpeech = true;
                trailingSilence = false;
                speechStart = begin;
            }
            continue;
        }

        // In speech. A frame back at or above the start gate is still active
        // speech and clears any trailing-silence timer.
        if (prob >= startThreshold) {
            trailingSilence = false;
            continue;
        }

        // Below the start gate. Start the silence timer only once the
        // probability has actually dropped under the end gate, so the tail of
        // a loud syllable does not begin counting prematurely. Once started,
        // every frame below the start gate (including the hysteresis band
        // between end and start) keeps accumulating toward the close.
        if (!trailingSilence) {
            if (prob < endThreshold) {
                trailingSilence = true;
                silenceStart = begin;
            }
            continue;
        }

        if (frameEnd(i) - silenceStart >= minSilenceSamples) {
            if (silenceStart - speechStart >= minSpeechSamples) {
                segments.push_back({speechStart, silenceStart, prob});
            }
            inSpeech = false;
            trailingSilence = false;
        }
    }

    if (inSpeech) {
        segments.push_back({speechStart, totalSamples, 1.0f});
    }
    return segments;
}

}  // namespace echoflow
