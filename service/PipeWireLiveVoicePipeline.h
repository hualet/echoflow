// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_PIPEWIRE_LIVE_VOICE_PIPELINE_H
#define ECHOFLOW_PIPEWIRE_LIVE_VOICE_PIPELINE_H

#include "AsrEngine.h"
#include "AudioSegmenter.h"
#include "Config.h"
#include "Interfaces.h"
#include "SegmentAsrWorker.h"
#include "SegmentTextAccumulator.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <sys/types.h>
#include <thread>
#include <vector>

namespace echoflow {

class PipeWireLiveVoicePipeline : public ILiveVoicePipeline {
public:
    PipeWireLiveVoicePipeline(Config cfg, AsrEngine& asr);
    ~PipeWireLiveVoicePipeline() override;

    PipeWireLiveVoicePipeline(const PipeWireLiveVoicePipeline&) = delete;
    PipeWireLiveVoicePipeline& operator=(const PipeWireLiveVoicePipeline&) = delete;

    void start() override;
    std::string finish() override;
    void cancel() override;
    void setPartialTextCallback(std::function<void(const std::string&)> callback) override;

private:
    void readerLoop();
    void enqueueSegments(std::vector<AudioSegment> segments);
    void flushSegmenter();
    void handleSegmentText(int sequence, const std::string& text);
    std::string stableText() const;
    void clearStableText();
    std::chrono::steady_clock::duration asrFinishTimeout() const;
    void stopRecorder();
    void cleanupProcess();
    void closeReadFd();
    void joinThreads();
    void openDebugAudioFile();
    void appendDebugAudio(const std::vector<int16_t>& samples);
    void finalizeDebugAudioFile();

    Config cfg_;
    AsrEngine& asr_;
    std::unique_ptr<AudioSegmenter> segmenter_;
    std::unique_ptr<SegmentAsrWorker> segmentWorker_;
    pid_t child_ = -1;
    int readFd_ = -1;
    std::thread readerThread_;
    std::function<void(const std::string&)> partialTextCallback_;
    std::ofstream debugAudio_;
    std::filesystem::path debugAudioPath_;
    uint32_t debugAudioBytes_ = 0;
    mutable std::mutex textMutex_;
    SegmentTextAccumulator textAccumulator_;
    std::string stableText_;
    mutable std::mutex callbackMutex_;
    std::chrono::steady_clock::time_point startedAt_{};
    std::atomic<bool> active_{false};
    std::atomic<bool> cancelled_{false};
};

}  // namespace echoflow

#endif  // ECHOFLOW_PIPEWIRE_LIVE_VOICE_PIPELINE_H
