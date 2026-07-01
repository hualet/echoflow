// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_SEGMENT_ASR_WORKER_H
#define ECHOFLOW_SEGMENT_ASR_WORKER_H

#include "AudioSegmenter.h"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace echoflow {

class SegmentAsrWorker {
public:
    using Transcribe = std::function<std::string(const AudioSegment&)>;
    using ResultCallback = std::function<void(const std::vector<std::string>&)>;

    explicit SegmentAsrWorker(Transcribe transcribe, ResultCallback callback = {});
    ~SegmentAsrWorker();

    SegmentAsrWorker(const SegmentAsrWorker&) = delete;
    SegmentAsrWorker& operator=(const SegmentAsrWorker&) = delete;

    void start();
    bool enqueue(AudioSegment segment);
    std::vector<std::string> finish();
    void cancel();

    size_t enqueuedCount() const;
    size_t completedCount() const;
    size_t highWaterMark() const;

private:
    void run();

    Transcribe transcribe_;
    ResultCallback callback_;
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<AudioSegment> queue_;
    std::vector<std::string> results_;
    std::thread thread_;
    std::exception_ptr error_;
    size_t enqueuedCount_ = 0;
    size_t completedCount_ = 0;
    size_t highWaterMark_ = 0;
    bool started_ = false;
    bool accepting_ = false;
    bool cancelled_ = false;
};

}  // namespace echoflow

#endif
