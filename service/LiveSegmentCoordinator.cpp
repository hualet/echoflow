// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "LiveSegmentCoordinator.h"

#include "log.h"

#include <utility>

namespace echoflow {

LiveSegmentCoordinator::LiveSegmentCoordinator(
    AudioSegmenterConfig config, SegmentAsrWorker::Transcribe transcribe,
    SegmentAsrWorker::ResultCallback callback)
    : segmenter_(config)
    , worker_(std::move(transcribe), std::move(callback))
{
}

void LiveSegmentCoordinator::start()
{
    worker_.start();
    active_ = true;
}

bool LiveSegmentCoordinator::append(const int16_t* samples, size_t count)
{
    if (!active_ || !samples || count == 0) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        metrics_.inputSamples += count;
    }
    for (AudioSegment& segment : segmenter_.append(samples, count)) {
        if (!enqueue(std::move(segment))) {
            return false;
        }
    }
    return true;
}

std::vector<std::string> LiveSegmentCoordinator::finish()
{
    if (active_) {
        const AudioSegmenterDiagnostics diagnostics = segmenter_.diagnostics();
        log("live VAD energy: frames=" + std::to_string(diagnostics.frameCount)
            + ", min_rms=" + std::to_string(diagnostics.minFrameRms)
            + ", max_rms=" + std::to_string(diagnostics.maxFrameRms)
            + ", below40_ms=" + std::to_string(diagnostics.longestBelow40Ms)
            + ", below80_ms=" + std::to_string(diagnostics.longestBelow80Ms)
            + ", below120_ms=" + std::to_string(diagnostics.longestBelow120Ms)
            + ", below200_ms=" + std::to_string(diagnostics.longestBelow200Ms));
        if (auto remaining = segmenter_.flush()) {
            enqueue(std::move(*remaining));
        }
        active_ = false;
    }
    std::vector<std::string> results = worker_.finish();
    std::lock_guard<std::mutex> lock(mutex_);
    metrics_.asrQueueHighWaterMark = worker_.highWaterMark();
    return results;
}

void LiveSegmentCoordinator::cancel()
{
    active_ = false;
    segmenter_.reset();
    worker_.cancel();
}

LivePipelineMetrics LiveSegmentCoordinator::metrics() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    LivePipelineMetrics result = metrics_;
    result.asrQueueHighWaterMark = worker_.highWaterMark();
    return result;
}

bool LiveSegmentCoordinator::enqueue(AudioSegment segment)
{
    const size_t sampleCount = segment.sampleCount();
    const uint64_t beginSample = segment.beginSample;
    const uint64_t endSample = segment.endSample;
    const int sampleRate = segment.sampleRate;
    if (!worker_.enqueue(std::move(segment))) {
        std::lock_guard<std::mutex> lock(mutex_);
        metrics_.audioDropped = true;
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    metrics_.segmentSamples += sampleCount;
    ++metrics_.enqueuedSegments;
    log("live segment queued: index=" + std::to_string(metrics_.enqueuedSegments)
        + ", begin_ms=" + std::to_string(beginSample * 1000 / sampleRate)
        + ", end_ms=" + std::to_string(endSample * 1000 / sampleRate)
        + ", audio_ms=" + std::to_string(sampleCount * 1000 / sampleRate));
    return true;
}

}  // namespace echoflow
