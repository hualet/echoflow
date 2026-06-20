// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_SEGMENT_ASR_WORKER_H
#define ECHOFLOW_SEGMENT_ASR_WORKER_H

#include "AudioSegmenter.h"
#include "Interfaces.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace echoflow {

class SegmentAsrWorker {
public:
    using ResultCallback = std::function<void(int sequence, const std::string& text)>;

    SegmentAsrWorker(IAsrEngine& asr, std::filesystem::path tempDir, ResultCallback callback);
    ~SegmentAsrWorker();

    void start();
    void enqueue(AudioSegment segment);
    bool finishAndWait(std::chrono::steady_clock::duration timeout);
    void cancelAndWait();

private:
    struct State;
    struct QueuedSegment {
        int sequence = 0;
        AudioSegment segment;
    };

    static void run(std::shared_ptr<State> state);
    static std::filesystem::path writeWav(const State& state, int sequence,
                                          const AudioSegment& segment);
    void joinThread();
    void detachThread();

    std::shared_ptr<State> state_;
    std::thread thread_;
};

}  // namespace echoflow

#endif
