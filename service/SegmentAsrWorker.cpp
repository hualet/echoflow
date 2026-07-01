// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SegmentAsrWorker.h"

#include "log.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace echoflow {

SegmentAsrWorker::SegmentAsrWorker(Transcribe transcribe, ResultCallback callback)
    : transcribe_(std::move(transcribe))
    , callback_(std::move(callback))
{
    if (!transcribe_) {
        throw std::invalid_argument("segment transcriber is required");
    }
}

SegmentAsrWorker::~SegmentAsrWorker()
{
    cancel();
}

void SegmentAsrWorker::start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (started_) {
        return;
    }
    started_ = true;
    accepting_ = true;
    cancelled_ = false;
    thread_ = std::thread(&SegmentAsrWorker::run, this);
}

bool SegmentAsrWorker::enqueue(AudioSegment segment)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!accepting_ || cancelled_) {
        return false;
    }
    queue_.push_back(std::move(segment));
    ++enqueuedCount_;
    highWaterMark_ = std::max(highWaterMark_, queue_.size());
    ready_.notify_one();
    return true;
}

std::vector<std::string> SegmentAsrWorker::finish()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        accepting_ = false;
        ready_.notify_all();
    }
    if (thread_.joinable()) {
        thread_.join();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    started_ = false;
    if (error_) {
        std::rethrow_exception(error_);
    }
    return results_;
}

void SegmentAsrWorker::cancel()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        accepting_ = false;
        cancelled_ = true;
        queue_.clear();
        results_.clear();
        ready_.notify_all();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    started_ = false;
}

size_t SegmentAsrWorker::enqueuedCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return enqueuedCount_;
}

size_t SegmentAsrWorker::completedCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return completedCount_;
}

size_t SegmentAsrWorker::highWaterMark() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return highWaterMark_;
}

void SegmentAsrWorker::run()
{
    while (true) {
        AudioSegment segment;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            ready_.wait(lock, [this] {
                return cancelled_ || !queue_.empty() || !accepting_;
            });
            if (cancelled_ || (queue_.empty() && !accepting_)) {
                return;
            }
            segment = std::move(queue_.front());
            queue_.pop_front();
        }

        std::string text;
        const auto startedAt = std::chrono::steady_clock::now();
        try {
            text = transcribe_(segment);
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!error_) {
                error_ = std::current_exception();
            }
        }

        std::vector<std::string> published;
        size_t pendingCount = 0;
        const size_t characterBytes = text.size();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++completedCount_;
            pendingCount = queue_.size();
            if (!text.empty()) {
                results_.push_back(std::move(text));
                published = results_;
            }
        }
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startedAt).count();
        log("live segment ASR finished: audio_ms="
            + std::to_string(static_cast<long long>(segment.durationSeconds() * 1000.0))
            + ", asr_ms=" + std::to_string(elapsedMs)
            + ", chars=" + std::to_string(characterBytes)
            + ", pending=" + std::to_string(pendingCount));
        if (!published.empty() && callback_) {
            callback_(published);
        }
    }
}

}  // namespace echoflow
