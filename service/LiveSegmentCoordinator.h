// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_LIVE_SEGMENT_COORDINATOR_H
#define ECHOFLOW_LIVE_SEGMENT_COORDINATOR_H

#include "AudioSegmenter.h"
#include "SegmentAsrWorker.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
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
    LiveSegmentCoordinator(AudioSegmenterConfig config,
                           SegmentAsrWorker::Transcribe transcribe,
                           SegmentAsrWorker::ResultCallback callback = {});

    void start();
    bool append(const int16_t* samples, size_t count);
    std::vector<std::string> finish();
    void cancel();
    LivePipelineMetrics metrics() const;

private:
    bool enqueue(AudioSegment segment);

    AudioSegmenter segmenter_;
    SegmentAsrWorker worker_;
    mutable std::mutex mutex_;
    LivePipelineMetrics metrics_;
    bool active_ = false;
};

}  // namespace echoflow

#endif
