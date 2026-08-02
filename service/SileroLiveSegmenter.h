// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_SILERO_LIVE_SEGMENTER_H
#define ECHOFLOW_SILERO_LIVE_SEGMENTER_H

#include "LiveSegmenter.h"
#include "VadBackend.h"

#include <memory>
#include <vector>

namespace echoflow {

struct SileroLiveSegmenterConfig {
    int sampleRate = 16000;
    int analysisCadenceMs = 100;
    int endSilenceMs = 300;
    int prePaddingMs = 200;
    int postPaddingMs = 200;
    int maxSegmentMs = 8000;
    int forceOverlapMs = 500;
};

class SileroLiveSegmenter final : public ILiveSegmenter {
public:
    SileroLiveSegmenter(SileroLiveSegmenterConfig config,
                        std::unique_ptr<IVadBackend> backend);

    std::vector<AudioSegment> append(const int16_t* samples, size_t count) override;
    std::optional<AudioSegment> flush() override;
    void reset() override;
    std::string name() const override;
    std::string diagnosticSummary() const override;

private:
    size_t samplesForMs(int milliseconds) const;
    std::vector<SpeechInterval> detect();
    std::vector<AudioSegment> analyze();
    AudioSegment makeSegment(size_t begin, size_t end) const;
    void erasePrefix(size_t count);

    SileroLiveSegmenterConfig config_;
    std::unique_ptr<IVadBackend> backend_;
    size_t analysisCadenceSamples_ = 0;
    size_t endSilenceSamples_ = 0;
    size_t prePaddingSamples_ = 0;
    size_t postPaddingSamples_ = 0;
    size_t maxSegmentSamples_ = 0;
    size_t forceOverlapSamples_ = 0;
    size_t samplesSinceAnalysis_ = 0;
    uint64_t bufferStartSample_ = 0;
    std::vector<int16_t> buffer_;
    size_t analysisCount_ = 0;
    double analysisMilliseconds_ = 0.0;
};

}  // namespace echoflow

#endif
