// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "LiveSegmentCoordinator.h"

#include "log.h"

#include <algorithm>
#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>

namespace echoflow {

LiveSegmentCoordinator::LiveSegmentCoordinator(
    std::unique_ptr<ILiveSegmenter> segmenter,
    SegmentAsrWorker::Transcribe transcribe,
    SegmentAsrWorker::ResultCallback callback,
    size_t maxPendingAudioSamples)
    : segmenter_(std::move(segmenter))
    , worker_(std::move(transcribe), std::move(callback))
    , maxPendingAudioSamples_(maxPendingAudioSamples)
{
    if (!segmenter_) {
        throw std::invalid_argument("live segmenter must not be null");
    }
    if (maxPendingAudioSamples_ == 0) {
        throw std::invalid_argument("pending live audio capacity must be positive");
    }
}

LiveSegmentCoordinator::LiveSegmentCoordinator(
    AudioSegmenterConfig config, SegmentAsrWorker::Transcribe transcribe,
    SegmentAsrWorker::ResultCallback callback,
    size_t maxPendingAudioSamples)
    : LiveSegmentCoordinator(std::make_unique<AudioSegmenter>(config),
                             std::move(transcribe), std::move(callback),
                             maxPendingAudioSamples)
{
}

LiveSegmentCoordinator::~LiveSegmentCoordinator()
{
    cancel();
}

void LiveSegmentCoordinator::start()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_) return;
        active_ = true;
        acceptingAudio_ = true;
        cancelled_ = false;
        segmentationError_ = nullptr;
    }
    log("live VAD backend: " + segmenter_->name());
    worker_.start();
    try {
        segmentationThread_ = std::thread(&LiveSegmentCoordinator::runSegmentation, this);
    } catch (...) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            active_ = false;
            acceptingAudio_ = false;
            cancelled_ = true;
        }
        worker_.cancel();
        throw;
    }
}

bool LiveSegmentCoordinator::append(const int16_t* samples, size_t count)
{
    if (!samples || count == 0) {
        return false;
    }
    std::vector<int16_t> copy(samples, samples + count);
    std::lock_guard<std::mutex> lock(mutex_);
    if (segmentationError_) {
        std::rethrow_exception(segmentationError_);
    }
    if (!active_ || !acceptingAudio_ || cancelled_) {
        return false;
    }
    metrics_.inputSamples += count;
    if (pendingAudioSamples_ > maxPendingAudioSamples_
        || count > maxPendingAudioSamples_ - pendingAudioSamples_) {
        segmentationError_ = std::make_exception_ptr(
            std::runtime_error("live segmentation audio queue overflow"));
        metrics_.audioDropped = true;
        acceptingAudio_ = false;
        cancelled_ = true;
        audioQueue_.clear();
        pendingAudioSamples_ = 0;
        audioReady_.notify_all();
        std::rethrow_exception(segmentationError_);
    }
    pendingAudioSamples_ += count;
    metrics_.segmentationQueueHighWaterSamples = std::max(
        metrics_.segmentationQueueHighWaterSamples, pendingAudioSamples_);
    audioQueue_.push_back(std::move(copy));
    audioReady_.notify_one();
    return true;
}

std::vector<std::string> LiveSegmentCoordinator::finish()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        acceptingAudio_ = false;
        active_ = false;
        audioReady_.notify_all();
    }
    if (segmentationThread_.joinable()) {
        segmentationThread_.join();
    }
    size_t segmentationQueuePeak = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        segmentationQueuePeak = metrics_.segmentationQueueHighWaterSamples;
    }
    log("live VAD " + segmenter_->name() + ": "
        + segmenter_->diagnosticSummary()
        + ", segmentation_queue_peak_samples="
        + std::to_string(segmentationQueuePeak));

    std::exception_ptr segmentationError;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        segmentationError = segmentationError_;
    }
    if (segmentationError) {
        worker_.cancel();
        std::rethrow_exception(segmentationError);
    }
    std::vector<std::string> results = worker_.finish();
    std::lock_guard<std::mutex> lock(mutex_);
    metrics_.asrQueueHighWaterMark = worker_.highWaterMark();
    return results;
}

void LiveSegmentCoordinator::cancel()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_ = false;
        acceptingAudio_ = false;
        cancelled_ = true;
        audioQueue_.clear();
        pendingAudioSamples_ = 0;
        audioReady_.notify_all();
    }
    if (segmentationThread_.joinable()) {
        segmentationThread_.join();
    }
    segmenter_->reset();
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

void LiveSegmentCoordinator::runSegmentation()
{
    try {
        while (true) {
            std::vector<int16_t> audio;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                audioReady_.wait(lock, [this] {
                    return cancelled_ || !audioQueue_.empty() || !acceptingAudio_;
                });
                if (cancelled_) return;
                if (audioQueue_.empty()) {
                    if (!acceptingAudio_) break;
                    continue;
                }
                audio = std::move(audioQueue_.front());
                audioQueue_.pop_front();
                pendingAudioSamples_ -= audio.size();
            }

            for (AudioSegment& segment : segmenter_->append(audio.data(), audio.size())) {
                if (!enqueue(std::move(segment))) {
                    throw std::runtime_error("live ASR worker stopped accepting segments");
                }
            }
        }

        if (auto remaining = segmenter_->flush()) {
            if (!enqueue(std::move(*remaining))) {
                throw std::runtime_error("live ASR worker stopped accepting final segment");
            }
        }
    } catch (...) {
        recordSegmentationError(std::current_exception());
    }
}

void LiveSegmentCoordinator::recordSegmentationError(std::exception_ptr error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!segmentationError_) segmentationError_ = std::move(error);
    acceptingAudio_ = false;
    cancelled_ = true;
    audioQueue_.clear();
    pendingAudioSamples_ = 0;
    metrics_.audioDropped = true;
    audioReady_.notify_all();
}

}  // namespace echoflow
