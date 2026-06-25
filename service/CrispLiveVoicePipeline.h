// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_CRISP_LIVE_VOICE_PIPELINE_H
#define ECHOFLOW_CRISP_LIVE_VOICE_PIPELINE_H

#include "AudioSegmenter.h"
#include "Config.h"
#include "Interfaces.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <sys/types.h>
#include <thread>
#include <vector>

namespace echoflow {

class CrispSession;

class CrispLiveVoicePipeline : public ILiveVoicePipeline {
public:
    explicit CrispLiveVoicePipeline(Config cfg);
    ~CrispLiveVoicePipeline() override;

    CrispLiveVoicePipeline(const CrispLiveVoicePipeline&) = delete;
    CrispLiveVoicePipeline& operator=(const CrispLiveVoicePipeline&) = delete;

    void start() override;
    std::string finish() override;
    void cancel() override;
    void setPartialTextCallback(std::function<void(const std::string&)> callback) override;

private:
    void readerLoop();
    void stopRecorder();
    void reapChild(pid_t& child);
    void emitText(const std::string& text);

    Config cfg_;
    std::unique_ptr<CrispSession> session_;
    pid_t recorderChild_ = -1;
    int readFd_ = -1;
    std::thread readerThread_;
    std::function<void(const std::string&)> partialTextCallback_;
    mutable std::mutex callbackMutex_;

    mutable std::mutex segmentMutex_;
    std::unique_ptr<AudioSegmenter> segmenter_;
    std::vector<std::string> results_;

    std::atomic<bool> active_{false};
    std::atomic<bool> cancelled_{false};
    std::chrono::steady_clock::time_point startedAt_{};
};

}  // namespace echoflow

#endif
