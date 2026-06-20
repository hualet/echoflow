// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AudioSegmenter.h"

#include <algorithm>
#include <cmath>

namespace echoflow {

size_t AudioSegment::sampleCount() const {
    return samples.size();
}

double AudioSegment::durationSeconds() const {
    if (sampleRate <= 0) {
        return 0.0;
    }

    return static_cast<double>(samples.size()) / static_cast<double>(sampleRate);
}

AudioSegmenter::AudioSegmenter(AudioSegmenterConfig config)
    : config_(config),
      frameSamples_(samplesForMs(config_.frameMs)),
      endSilenceSamples_(samplesForMs(config_.endSilenceMs)),
      minSegmentSamples_(samplesForMs(config_.minSegmentMs)),
      prePaddingSamples_(samplesForMs(config_.prePaddingMs)),
      postPaddingSamples_(samplesForMs(config_.postPaddingMs)),
      maxSegmentSamples_(samplesForMs(config_.maxSegmentMs)) {
}

std::vector<AudioSegment> AudioSegmenter::append(const int16_t* samples, size_t count) {
    std::vector<AudioSegment> emitted;
    if (!samples || count == 0 || frameSamples_ == 0) {
        return emitted;
    }

    pendingSamples_.insert(pendingSamples_.end(), samples, samples + count);
    while (pendingSamples_.size() >= frameSamples_) {
        const double frameRms = rms(pendingSamples_.data(), frameSamples_);
        const bool speech = isSpeech(frameRms);

        if (!active_) {
            if (!speech) {
                appendPrePadding(pendingSamples_.data(), frameSamples_);
                if (!hasNoiseFloor_) {
                    noiseFloor_ = std::max(1.0, frameRms);
                    hasNoiseFloor_ = true;
                } else {
                    noiseFloor_ = noiseFloor_ * 0.95 + std::max(1.0, frameRms) * 0.05;
                }
            } else {
                active_ = true;
                speechSamples_ = frameSamples_;
                trailingSilenceSamples_ = 0;
                segmentSamples_ = prePadding_;
                segmentSamples_.insert(segmentSamples_.end(),
                                       pendingSamples_.begin(),
                                       pendingSamples_.begin()
                                           + static_cast<std::ptrdiff_t>(frameSamples_));
                prePadding_.clear();
            }
        } else {
            segmentSamples_.insert(segmentSamples_.end(),
                                   pendingSamples_.begin(),
                                   pendingSamples_.begin()
                                       + static_cast<std::ptrdiff_t>(frameSamples_));
            if (speech) {
                speechSamples_ += frameSamples_;
                trailingSilenceSamples_ = 0;
            } else {
                trailingSilenceSamples_ += frameSamples_;
            }

            if (speechSamples_ < minSegmentSamples_
                && trailingSilenceSamples_ >= endSilenceSamples_) {
                discardActiveSegmentToPrePadding();
            } else if (maxSegmentSamples_ > 0 && segmentSamples_.size() >= maxSegmentSamples_) {
                emitted.push_back(makeSegment(maxSegmentSamples_));
                segmentSamples_.erase(segmentSamples_.begin(),
                                      segmentSamples_.begin()
                                          + static_cast<std::ptrdiff_t>(maxSegmentSamples_));
                active_ = !segmentSamples_.empty();
                speechSamples_ = active_ ? segmentSamples_.size() : 0;
                trailingSilenceSamples_ = 0;
            } else if (speechSamples_ >= minSegmentSamples_
                       && trailingSilenceSamples_ >= endSilenceSamples_) {
                std::optional<AudioSegment> segment = sealWithPostPadding();
                if (segment.has_value()) {
                    emitted.push_back(std::move(*segment));
                }
            }
        }

        pendingSamples_.erase(pendingSamples_.begin(),
                              pendingSamples_.begin()
                                  + static_cast<std::ptrdiff_t>(frameSamples_));
    }

    return emitted;
}

std::optional<AudioSegment> AudioSegmenter::flush() {
    if (!pendingSamples_.empty()) {
        const std::vector<int16_t> pending = pendingSamples_;
        pendingSamples_.clear();
        if (active_) {
            segmentSamples_.insert(segmentSamples_.end(), pending.begin(), pending.end());
        } else {
            appendPrePadding(pending.data(), pending.size());
        }
    }

    if (!active_ || speechSamples_ < minSegmentSamples_) {
        reset();
        return std::nullopt;
    }

    AudioSegment segment = makeSegment(segmentSamples_.size());
    reset();
    return segment;
}

void AudioSegmenter::reset() {
    hasNoiseFloor_ = false;
    noiseFloor_ = 1.0;
    active_ = false;
    speechSamples_ = 0;
    trailingSilenceSamples_ = 0;
    pendingSamples_.clear();
    prePadding_.clear();
    segmentSamples_.clear();
}

size_t AudioSegmenter::samplesForMs(int milliseconds) const {
    if (config_.sampleRate <= 0 || milliseconds <= 0) {
        return 0;
    }

    return static_cast<size_t>((static_cast<int64_t>(config_.sampleRate) * milliseconds) / 1000);
}

double AudioSegmenter::rms(const int16_t* samples, size_t count) const {
    if (!samples || count == 0) {
        return 0.0;
    }

    double squareSum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        const double value = static_cast<double>(samples[i]);
        squareSum += value * value;
    }

    return std::sqrt(squareSum / static_cast<double>(count));
}

bool AudioSegmenter::isSpeech(double frameRms) const {
    const double threshold = std::max(config_.minSpeechRms, noiseFloor_ * config_.speechRatio);
    return frameRms >= threshold;
}

void AudioSegmenter::appendPrePadding(const int16_t* samples, size_t count) {
    if (!samples || count == 0 || prePaddingSamples_ == 0) {
        return;
    }

    prePadding_.insert(prePadding_.end(), samples, samples + count);
    trimPrePadding();
}

void AudioSegmenter::discardActiveSegmentToPrePadding() {
    const std::vector<int16_t> discarded = segmentSamples_;
    active_ = false;
    speechSamples_ = 0;
    trailingSilenceSamples_ = 0;
    segmentSamples_.clear();
    prePadding_.clear();
    appendPrePadding(discarded.data(), discarded.size());
}

std::optional<AudioSegment> AudioSegmenter::sealWithPostPadding() {
    if (!active_ || speechSamples_ < minSegmentSamples_) {
        discardActiveSegmentToPrePadding();
        return std::nullopt;
    }

    const size_t extraSilence =
        trailingSilenceSamples_ > postPaddingSamples_ ? trailingSilenceSamples_ - postPaddingSamples_ : 0;
    const size_t segmentCount = segmentSamples_.size() - std::min(extraSilence, segmentSamples_.size());
    AudioSegment segment = makeSegment(segmentCount);

    const std::vector<int16_t> leftover(segmentSamples_.begin()
                                            + static_cast<std::ptrdiff_t>(segmentCount),
                                        segmentSamples_.end());
    active_ = false;
    speechSamples_ = 0;
    trailingSilenceSamples_ = 0;
    segmentSamples_.clear();
    prePadding_.clear();
    appendPrePadding(leftover.data(), leftover.size());
    return segment;
}

AudioSegment AudioSegmenter::makeSegment(size_t count) const {
    AudioSegment segment;
    segment.sampleRate = config_.sampleRate;
    const size_t actualCount = std::min(count, segmentSamples_.size());
    segment.samples.assign(segmentSamples_.begin(),
                           segmentSamples_.begin() + static_cast<std::ptrdiff_t>(actualCount));
    return segment;
}

void AudioSegmenter::trimPrePadding() {
    if (prePadding_.size() <= prePaddingSamples_) {
        return;
    }

    prePadding_.erase(prePadding_.begin(),
                      prePadding_.begin()
                          + static_cast<std::ptrdiff_t>(prePadding_.size() - prePaddingSamples_));
}

}  // namespace echoflow
