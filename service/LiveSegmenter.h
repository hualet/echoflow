// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_LIVE_SEGMENTER_H
#define ECHOFLOW_LIVE_SEGMENTER_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace echoflow {

struct AudioSegment {
    int sampleRate = 16000;
    uint64_t beginSample = 0;
    uint64_t endSample = 0;
    std::vector<int16_t> samples;

    size_t sampleCount() const { return samples.size(); }
    double durationSeconds() const
    {
        return sampleRate > 0
            ? static_cast<double>(samples.size()) / static_cast<double>(sampleRate)
            : 0.0;
    }
};

class ILiveSegmenter {
public:
    virtual ~ILiveSegmenter() = default;

    virtual std::vector<AudioSegment> append(const int16_t* samples, size_t count) = 0;
    virtual std::optional<AudioSegment> flush() = 0;
    virtual void reset() = 0;
    virtual std::string name() const = 0;
    virtual std::string diagnosticSummary() const = 0;
};

}  // namespace echoflow

#endif
