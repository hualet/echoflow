// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_CRISP_LIVE_VOICE_PIPELINE_H
#define ECHOFLOW_CRISP_LIVE_VOICE_PIPELINE_H

#include "CrispStreamAccumulator.h"
#include "Config.h"
#include "Interfaces.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <sys/types.h>
#include <thread>

namespace echoflow {

// Live voice pipeline on top of CrispASR's native --stream --stream-json mode.
// pw-record's raw PCM is piped straight into a long-lived crispasr child's
// stdin (OS pipe, no in-process forwarding); a parser thread reads JSON events
// from crispasr's stdout and drives the partial-text callback.
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

    static std::vector<std::string> buildCrispArgs(const Config& cfg);

private:
    void parserLoop();
    void stopRecorder();
    void reapChild(pid_t& child);
    void emitText(const std::string& text);

    Config cfg_;
    pid_t recorderChild_ = -1;
    pid_t crispChild_ = -1;
    int crispOutFd_ = -1;
    std::thread parserThread_;
    std::function<void(const std::string&)> partialTextCallback_;
    mutable std::mutex callbackMutex_;
    CrispStreamAccumulator accumulator_;
    mutable std::mutex accumulatorMutex_;
    std::atomic<bool> active_{false};
    std::atomic<bool> cancelled_{false};
    std::chrono::steady_clock::time_point startedAt_{};
};

}  // namespace echoflow

#endif
