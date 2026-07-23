// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_VOICE_SESSION_H
#define ECHOFLOW_VOICE_SESSION_H

#include "Config.h"
#include "Interfaces.h"

#include <cstdint>
#include <mutex>
#include <thread>

namespace echoflow {

enum class SessionState {
    Idle,
    Recording,
    Transcribing,
    Cancelling,
};

class VoiceSession {
public:
    VoiceSession(
        Config cfg, IRecorder& recorder, IAsrEngine& asr, ICommitter& committer, IUiNotifier& ui);
    VoiceSession(
        Config cfg, ILiveVoicePipeline& livePipeline, ICommitter& committer, IUiNotifier& ui);
    ~VoiceSession();

    std::string handleCommand(const std::string& command);
    SessionState state() const;

private:
    std::string startRecording();
    std::string startFinalization();
    void finalize(std::uint64_t generation);
    void joinCompletedWorker();
    // Discard any in-flight recording and return to Idle, emitting IDLE so the
    // UI hides the capsule. Shared by BLUR (focus lost) and CANCEL (X button).
    void cancelActiveSession();
    static std::string phaseName(SessionState state);

    Config cfg_;
    IRecorder* recorder_ = nullptr;
    IAsrEngine* asr_ = nullptr;
    ILiveVoicePipeline* livePipeline_ = nullptr;
    ICommitter& committer_;
    IUiNotifier& ui_;

    mutable std::mutex mutex_;
    SessionState state_ = SessionState::Idle;
    std::thread finalizationWorker_;
    std::uint64_t nextGeneration_ = 0;
    std::uint64_t runningGeneration_ = 0;
    bool finalizationRunning_ = false;
    bool cancelRequested_ = false;
    bool shuttingDown_ = false;
    // True while a live (streaming) session is active so late STREAM_TEXT
    // datagrams arriving after a CANCEL/TRANSCRIBING are ignored.
    bool recordingStreamActive_ = false;
};

} // namespace echoflow

#endif
