// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SileroLiveSegmenter.h"

#include "log.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace echoflow {

namespace {

using Clock = std::chrono::steady_clock;

}  // namespace

SileroLiveSegmenter::SileroLiveSegmenter(
    SileroLiveSegmenterConfig config, std::unique_ptr<IVadBackend> backend)
    : config_(config)
    , backend_(std::move(backend))
    , analysisCadenceSamples_(samplesForMs(config_.analysisCadenceMs))
    , retainedHistorySamples_(std::max(
          samplesForMs(config_.prePaddingMs),
          samplesForMs(config_.minSpeechMs) + samplesForMs(32)))
    , prePaddingSamples_(samplesForMs(config_.prePaddingMs))
    , postPaddingSamples_(samplesForMs(config_.postPaddingMs))
    , maxSegmentSamples_(samplesForMs(config_.maxSegmentMs))
    , forceOverlapSamples_(samplesForMs(config_.forceOverlapMs))
{
    if (!backend_) {
        throw std::invalid_argument("Silero live VAD backend must not be null");
    }
    if (config_.sampleRate != 16000) {
        throw std::invalid_argument("Silero live VAD requires 16000 Hz audio");
    }
    if (analysisCadenceSamples_ == 0 || config_.minSpeechMs <= 0
        || maxSegmentSamples_ == 0) {
        throw std::invalid_argument("Silero live VAD durations must be positive");
    }
    if (config_.prePaddingMs < 0 || config_.postPaddingMs < 0
        || config_.forceOverlapMs < 0 || forceOverlapSamples_ >= maxSegmentSamples_) {
        throw std::invalid_argument("invalid Silero live VAD padding or overlap");
    }
}

std::vector<AudioSegment> SileroLiveSegmenter::append(
    const int16_t* samples, size_t count)
{
    if (!samples || count == 0) return {};

    buffer_.insert(buffer_.end(), samples, samples + count);
    samplesSinceAnalysis_ += count;
    if (samplesSinceAnalysis_ < analysisCadenceSamples_
        && buffer_.size() < maxSegmentSamples_) {
        return {};
    }
    samplesSinceAnalysis_ %= analysisCadenceSamples_;

    std::vector<AudioSegment> emitted;
    while (!buffer_.empty()) {
        std::vector<AudioSegment> batch = analyze();
        if (batch.empty()) break;
        emitted.insert(emitted.end(),
                       std::make_move_iterator(batch.begin()),
                       std::make_move_iterator(batch.end()));
        if (buffer_.size() < maxSegmentSamples_) break;
    }
    return emitted;
}

std::optional<AudioSegment> SileroLiveSegmenter::flush()
{
    if (buffer_.empty()) {
        reset();
        return std::nullopt;
    }

    const std::vector<SpeechInterval> intervals = detect();
    size_t first = buffer_.size();
    size_t last = 0;
    for (const SpeechInterval& raw : intervals) {
        const size_t begin = std::min(raw.beginSample, buffer_.size());
        const size_t end = std::min(raw.endSample, buffer_.size());
        if (end <= begin) continue;
        first = std::min(first, begin);
        last = std::max(last, end);
    }
    if (last <= first) {
        reset();
        return std::nullopt;
    }

    const size_t begin = first > prePaddingSamples_ ? first - prePaddingSamples_ : 0;
    const size_t end = std::min(buffer_.size(), last + postPaddingSamples_);
    AudioSegment segment = makeSegment(begin, end);
    log("Silero VAD boundary: reason=flush, begin_ms="
        + std::to_string(segment.beginSample * 1000 / config_.sampleRate)
        + ", end_ms=" + std::to_string(segment.endSample * 1000 / config_.sampleRate));
    reset();
    return segment;
}

void SileroLiveSegmenter::reset()
{
    samplesSinceAnalysis_ = 0;
    bufferStartSample_ = 0;
    buffer_.clear();
    analysisCount_ = 0;
    analysisMilliseconds_ = 0.0;
}

std::string SileroLiveSegmenter::name() const
{
    return "silero";
}

std::string SileroLiveSegmenter::diagnosticSummary() const
{
    return "analyses=" + std::to_string(analysisCount_)
        + ", analysis_ms=" + std::to_string(analysisMilliseconds_)
        + ", buffered_ms="
        + std::to_string(buffer_.size() * 1000 / static_cast<size_t>(config_.sampleRate));
}

size_t SileroLiveSegmenter::samplesForMs(int milliseconds) const
{
    if (config_.sampleRate <= 0 || milliseconds <= 0) return 0;
    return static_cast<size_t>(
        static_cast<int64_t>(config_.sampleRate) * milliseconds / 1000);
}

std::vector<SpeechInterval> SileroLiveSegmenter::detect()
{
    if (buffer_.empty()) return {};
    const auto started = Clock::now();
    std::vector<SpeechInterval> intervals = backend_->detect(buffer_.data(), buffer_.size());
    analysisMilliseconds_ += std::chrono::duration<double, std::milli>(
        Clock::now() - started).count();
    ++analysisCount_;
    return intervals;
}

std::vector<AudioSegment> SileroLiveSegmenter::analyze()
{
    const std::vector<SpeechInterval> intervals = detect();
    std::vector<AudioSegment> emitted;
    size_t consumed = 0;
    size_t firstSpeech = buffer_.size();

    for (const SpeechInterval& raw : intervals) {
        const size_t begin = std::min(raw.beginSample, buffer_.size());
        const size_t end = std::min(raw.endSample, buffer_.size());
        if (end <= begin) continue;
        firstSpeech = std::min(firstSpeech, begin);
        if (end >= buffer_.size()) continue;

        const size_t paddedBegin = std::max(
            consumed, begin > prePaddingSamples_ ? begin - prePaddingSamples_ : size_t(0));
        const size_t paddedEnd = std::min(buffer_.size(), end + postPaddingSamples_);
        if (paddedEnd <= paddedBegin) continue;

        emitted.push_back(makeSegment(paddedBegin, paddedEnd));
        consumed = paddedEnd;
    }

    if (!emitted.empty()) {
        for (const AudioSegment& segment : emitted) {
            log("Silero VAD boundary: reason=natural, begin_ms="
                + std::to_string(segment.beginSample * 1000 / config_.sampleRate)
                + ", end_ms="
                + std::to_string(segment.endSample * 1000 / config_.sampleRate));
        }
        erasePrefix(consumed);
        return emitted;
    }

    if (firstSpeech == buffer_.size()) {
        if (buffer_.size() > retainedHistorySamples_) {
            erasePrefix(buffer_.size() - retainedHistorySamples_);
        }
        return {};
    }

    if (firstSpeech > prePaddingSamples_) {
        erasePrefix(firstSpeech - prePaddingSamples_);
    }
    if (buffer_.size() < maxSegmentSamples_) return {};

    AudioSegment forced = makeSegment(0, maxSegmentSamples_);
    log("Silero VAD boundary: reason=forced, begin_ms="
        + std::to_string(forced.beginSample * 1000 / config_.sampleRate)
        + ", end_ms=" + std::to_string(forced.endSample * 1000 / config_.sampleRate));
    emitted.push_back(std::move(forced));
    erasePrefix(maxSegmentSamples_ - forceOverlapSamples_);
    return emitted;
}

AudioSegment SileroLiveSegmenter::makeSegment(size_t begin, size_t end) const
{
    const size_t clampedBegin = std::min(begin, buffer_.size());
    const size_t clampedEnd = std::min(std::max(end, clampedBegin), buffer_.size());
    AudioSegment segment;
    segment.sampleRate = config_.sampleRate;
    segment.beginSample = bufferStartSample_ + clampedBegin;
    segment.endSample = bufferStartSample_ + clampedEnd;
    segment.samples.assign(
        buffer_.begin() + static_cast<std::ptrdiff_t>(clampedBegin),
        buffer_.begin() + static_cast<std::ptrdiff_t>(clampedEnd));
    return segment;
}

void SileroLiveSegmenter::erasePrefix(size_t count)
{
    const size_t actual = std::min(count, buffer_.size());
    buffer_.erase(buffer_.begin(),
                  buffer_.begin() + static_cast<std::ptrdiff_t>(actual));
    bufferStartSample_ += actual;
}

}  // namespace echoflow
