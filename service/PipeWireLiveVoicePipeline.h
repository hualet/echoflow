// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_PIPEWIRE_LIVE_VOICE_PIPELINE_H
#define ECHOFLOW_PIPEWIRE_LIVE_VOICE_PIPELINE_H

#include "AsrEngine.h"
#include "Config.h"
#include "Interfaces.h"
#include "LiveAudioBuffer.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <sys/types.h>
#include <thread>

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
    void asrLoop();
    void stopRecorder();
    void cleanupProcess();
    void closeReadFd();
    void joinThreads();

    Config cfg_;
    AsrEngine& asr_;
    std::unique_ptr<LiveAudioBuffer> live_;
    pid_t child_ = -1;
    int readFd_ = -1;
    std::thread readerThread_;
    std::thread asrThread_;
    std::string result_;
    std::string partialText_;
    std::function<void(const std::string&)> partialTextCallback_;
    std::mutex partialTextMutex_;
    std::chrono::steady_clock::time_point startedAt_{};
    std::atomic<bool> active_{false};
    std::atomic<bool> cancelled_{false};
};

}  // namespace echoflow

#endif  // ECHOFLOW_PIPEWIRE_LIVE_VOICE_PIPELINE_H
