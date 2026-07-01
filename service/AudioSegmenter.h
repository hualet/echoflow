// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_AUDIO_SEGMENTER_H
#define ECHOFLOW_AUDIO_SEGMENTER_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace echoflow {

struct AudioSegmenterConfig {
    int sampleRate = 16000;
    int frameMs = 20;
    int endSilenceMs = 500;
    int minSegmentMs = 400;
    int prePaddingMs = 200;
    int postPaddingMs = 200;
    int maxSegmentMs = 8000;
    int forceOverlapMs = 500;
    double speechRatio = 3.0;
    double minSpeechRms = 40.0;
    double minActiveSpeechRms = 80.0;
};

struct AudioSegment {
    int sampleRate = 16000;
    uint64_t beginSample = 0;
    uint64_t endSample = 0;
    std::vector<int16_t> samples;

    size_t sampleCount() const;
    double durationSeconds() const;
};

struct AudioSegmenterDiagnostics {
    size_t frameCount = 0;
    double minFrameRms = 0.0;
    double maxFrameRms = 0.0;
    int longestBelow40Ms = 0;
    int longestBelow80Ms = 0;
    int longestBelow120Ms = 0;
    int longestBelow200Ms = 0;
};

class AudioSegmenter {
public:
    explicit AudioSegmenter(AudioSegmenterConfig config);

    std::vector<AudioSegment> append(const int16_t* samples, size_t count);
    std::optional<AudioSegment> flush();
    void reset();
    AudioSegmenterDiagnostics diagnostics() const;

private:
    size_t samplesForMs(int milliseconds) const;
    double rms(const int16_t* samples, size_t count) const;
    bool isSpeech(double frameRms) const;
    void appendPrePadding(const int16_t* samples, size_t count);
    void discardActiveSegmentToPrePadding();
    std::optional<AudioSegment> sealWithPostPadding();
    AudioSegment makeSegment(size_t count) const;
    void trimPrePadding();

    AudioSegmenterConfig config_;
    size_t frameSamples_ = 0;
    size_t endSilenceSamples_ = 0;
    size_t minSegmentSamples_ = 0;
    size_t prePaddingSamples_ = 0;
    size_t postPaddingSamples_ = 0;
    size_t maxSegmentSamples_ = 0;
    size_t forceOverlapSamples_ = 0;

    double noiseFloor_ = 1.0;
    bool hasNoiseFloor_ = false;
    bool useElevatedEndpoint_ = false;
    bool active_ = false;
    size_t speechSamples_ = 0;
    size_t trailingSilenceSamples_ = 0;
    double trailingSilenceRmsSum_ = 0.0;
    size_t trailingSilenceFrames_ = 0;
    uint64_t processedSamples_ = 0;
    uint64_t segmentStartSample_ = 0;
    std::vector<int16_t> pendingSamples_;
    std::vector<int16_t> prePadding_;
    std::vector<int16_t> segmentSamples_;
    AudioSegmenterDiagnostics diagnostics_;
    size_t currentBelow40Frames_ = 0;
    size_t currentBelow80Frames_ = 0;
    size_t currentBelow120Frames_ = 0;
    size_t currentBelow200Frames_ = 0;
};

}  // namespace echoflow

#endif
