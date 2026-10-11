// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_SILERO_LIVE_SEGMENTER_H
#define ECHOFLOW_SILERO_LIVE_SEGMENTER_H

#include "LiveSegmenter.h"
#include "VadBackend.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace echoflow {

struct SileroLiveSegmenterConfig {
    int sampleRate = 16000;
    int analysisCadenceMs = 100;
    int minSpeechMs = 100;
    int prePaddingMs = 200;
    int postPaddingMs = 200;
    int maxSegmentMs = 8000;
    int forceOverlapMs = 500;
    // Audio kept ahead of any onset. Silero misses speech that starts near
    // the beginning of the analysed buffer, so trimming to the onset (or to
    // a short history while idle) silently dropped whole sentences.
    int leadInMs = 1000;
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
    void keepLeadInBefore(size_t position);
    size_t emittedOffset() const;
    void clearBuffer();

    SileroLiveSegmenterConfig config_;
    std::unique_ptr<IVadBackend> backend_;
    size_t analysisCadenceSamples_ = 0;
    size_t retainedHistorySamples_ = 0;
    size_t prePaddingSamples_ = 0;
    size_t postPaddingSamples_ = 0;
    size_t maxSegmentSamples_ = 0;
    size_t forceOverlapSamples_ = 0;
    size_t leadInSamples_ = 0;
    size_t samplesSinceAnalysis_ = 0;
    uint64_t bufferStartSample_ = 0;
    std::vector<int16_t> buffer_;
    // Absolute end of audio already handed out; kept lead-in before it must
    // never be emitted again.
    uint64_t emittedEndSample_ = 0;
    // Absolute onset of speech seen but not yet emitted. A later analysis
    // that reports nothing does not discard it.
    std::optional<uint64_t> openSpeechBegin_;
    size_t analysisCount_ = 0;
    double analysisMilliseconds_ = 0.0;
};

}  // namespace echoflow

#endif
