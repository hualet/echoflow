// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_VOICE_SESSION_H
#define ECHOFLOW_VOICE_SESSION_H

#include "Config.h"
#include "Interfaces.h"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace echoflow {

enum class SessionState {
    Idle,
    Recording,
    Transcribing,
    Cancelling,
};

class VoiceSession {
public:
    using ThreadLauncher = std::function<std::thread(std::function<void()>)>;

    VoiceSession(Config cfg, IRecorder& recorder, IAsrEngine& asr,
        ICommitter& committer, IUiNotifier& ui, ThreadLauncher threadLauncher = {});
    VoiceSession(Config cfg, ILiveVoicePipeline& livePipeline,
        ICommitter& committer, IUiNotifier& ui, ThreadLauncher threadLauncher = {});
    ~VoiceSession();

    // Optional; must be set before the first command. The polisher rewrites
    // the final transcript and falls back to the raw text on any failure.
    void setTextPolisher(ITextPolisher* polisher);

    std::string handleCommand(const std::string& command);
    SessionState state() const;

private:
    struct PartialCallbackState {
        std::mutex mutex;
        std::condition_variable condition;
        IUiNotifier* ui = nullptr;
        bool accepting = false;
        std::size_t active = 0;
    };

    struct PhaseNotification {
        SessionState state;
        std::uint64_t version;
        bool suppressIfStale;
    };

    std::string startRecording();
    std::string startFinalization();
    void finalize(std::uint64_t generation);
    std::string polishText(const std::string& text);
    void joinCompletedWorker();
    void setPartialTextAccepting(bool accepting);
    void shutdownPartialTextCallback();
    // Discard any in-flight recording and return to Idle, emitting IDLE so the
    // UI hides the capsule. Shared by BLUR (focus lost) and CANCEL (X button).
    std::string cancelActiveSession();
    std::uint64_t setStateLocked(SessionState state);
    std::pair<SessionState, std::uint64_t> phaseSnapshot() const;
    void publishPhase(
        SessionState state, std::uint64_t version, bool suppressIfStale = false);
    static std::string phaseName(SessionState state);

    Config cfg_;
    IRecorder* recorder_ = nullptr;
    IAsrEngine* asr_ = nullptr;
    ILiveVoicePipeline* livePipeline_ = nullptr;
    ITextPolisher* polisher_ = nullptr;
    ICommitter& committer_;
    IUiNotifier& ui_;
    ThreadLauncher threadLauncher_;
    std::shared_ptr<PartialCallbackState> partialCallbackState_;

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    SessionState state_ = SessionState::Idle;
    std::uint64_t stateVersion_ = 0;
    std::thread finalizationWorker_;
    std::uint64_t nextGeneration_ = 0;
    std::uint64_t runningGeneration_ = 0;
    bool finalizationRunning_ = false;
    bool finalizationStartPublished_ = false;
    bool commitClaimed_ = false;
    std::size_t pendingCancellationNotifications_ = 0;
    bool cancelRequested_ = false;
    bool shuttingDown_ = false;

    std::mutex phaseNotificationMutex_;
    std::deque<PhaseNotification> phaseNotifications_;
    bool phaseNotificationDraining_ = false;
};

} // namespace echoflow

#endif
