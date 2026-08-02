// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "LiveSegmenterFactory.h"

#include "AudioSegmenter.h"
#include "SileroLiveSegmenter.h"
#include "SileroVadBackend.h"

#include <stdexcept>
#include <utility>

namespace echoflow {

std::unique_ptr<ILiveSegmenter> makeLiveSegmenter(const Config& config)
{
    if (config.vadBackend == "energy") {
        AudioSegmenterConfig energy;
        energy.sampleRate = config.pwRecord.rate;
        return std::make_unique<AudioSegmenter>(energy);
    }
    if (config.vadBackend == "silero") {
        SileroLiveSegmenterConfig live;
        live.sampleRate = config.pwRecord.rate;
        live.minSpeechMs = config.vadMinSpeechMs;
        live.endSilenceMs = config.vadMinSilenceMs;
        auto backend = std::make_unique<SileroVadBackend>(
            config.vadModelPath, config.vadThreshold,
            config.vadMinSpeechMs, config.vadMinSilenceMs);
        return std::make_unique<SileroLiveSegmenter>(live, std::move(backend));
    }
    throw std::invalid_argument("unsupported VAD backend: " + config.vadBackend);
}

}  // namespace echoflow
