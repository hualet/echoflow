// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "LiveSegmentCoordinator.h"

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
    if (!worker_.enqueue(std::move(segment))) {
        std::lock_guard<std::mutex> lock(mutex_);
        metrics_.audioDropped = true;
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    metrics_.segmentSamples += sampleCount;
    ++metrics_.enqueuedSegments;
    return true;
}

}  // namespace echoflow
