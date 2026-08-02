// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_LIVE_SEGMENT_COORDINATOR_H
#define ECHOFLOW_LIVE_SEGMENT_COORDINATOR_H

#include "AudioSegmenter.h"
#include "LiveSegmenter.h"
#include "SegmentAsrWorker.h"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace echoflow {

struct LivePipelineMetrics {
    uint64_t inputSamples = 0;
    uint64_t segmentSamples = 0;
    size_t enqueuedSegments = 0;
    size_t asrQueueHighWaterMark = 0;
    bool audioDropped = false;
};

class LiveSegmentCoordinator {
public:
    LiveSegmentCoordinator(std::unique_ptr<ILiveSegmenter> segmenter,
                           SegmentAsrWorker::Transcribe transcribe,
                           SegmentAsrWorker::ResultCallback callback = {},
                           size_t maxPendingAudioSamples = 480000);
    LiveSegmentCoordinator(AudioSegmenterConfig config,
                           SegmentAsrWorker::Transcribe transcribe,
                           SegmentAsrWorker::ResultCallback callback = {},
                           size_t maxPendingAudioSamples = 480000);
    ~LiveSegmentCoordinator();

    void start();
    bool append(const int16_t* samples, size_t count);
    std::vector<std::string> finish();
    void cancel();
    LivePipelineMetrics metrics() const;

private:
    bool enqueue(AudioSegment segment);
    void runSegmentation();
    void recordSegmentationError(std::exception_ptr error);

    std::unique_ptr<ILiveSegmenter> segmenter_;
    SegmentAsrWorker worker_;
    mutable std::mutex mutex_;
    std::condition_variable audioReady_;
    std::deque<std::vector<int16_t>> audioQueue_;
    std::thread segmentationThread_;
    size_t pendingAudioSamples_ = 0;
    size_t maxPendingAudioSamples_ = 0;
    std::exception_ptr segmentationError_;
    LivePipelineMetrics metrics_;
    bool active_ = false;
    bool acceptingAudio_ = false;
    bool cancelled_ = false;
};

}  // namespace echoflow

#endif
