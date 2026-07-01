// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SegmentAsrWorker.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace echoflow {

SegmentAsrWorker::SegmentAsrWorker(Transcribe transcribe)
    : transcribe_(std::move(transcribe))
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
        try {
            text = transcribe_(segment);
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!error_) {
                error_ = std::current_exception();
            }
        }

        std::lock_guard<std::mutex> lock(mutex_);
        ++completedCount_;
        if (!text.empty()) {
            results_.push_back(std::move(text));
        }
    }
}

}  // namespace echoflow
