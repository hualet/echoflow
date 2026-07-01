// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_VOICE_SESSION_H
#define ECHOFLOW_VOICE_SESSION_H

#include "Config.h"
#include "Interfaces.h"

namespace echoflow {

enum class SessionState {
    Idle,
    Recording,
    Transcribing,
};

class VoiceSession {
public:
    VoiceSession(Config cfg, IRecorder& recorder, IAsrEngine& asr,
                 ICommitter& committer, IUiNotifier& ui);
    VoiceSession(Config cfg, ILiveVoicePipeline& livePipeline,
                 ICommitter& committer, IUiNotifier& ui);

    std::string handleCommand(const std::string& command);
    SessionState state() const { return state_; }

private:
    std::string startRecording();
    std::string stopTranscribeCommit();
    // Discard any in-flight recording and return to Idle, emitting IDLE so the
    // UI hides the capsule. Shared by BLUR (focus lost) and CANCEL (X button).
    void cancelRecording();

    Config cfg_;
    IRecorder* recorder_ = nullptr;
    IAsrEngine* asr_ = nullptr;
    ILiveVoicePipeline* livePipeline_ = nullptr;
    ICommitter& committer_;
    IUiNotifier& ui_;
    SessionState state_ = SessionState::Idle;
    // True while a live (streaming) session is active so late STREAM_TEXT
    // datagrams arriving after a CANCEL/TRANSCRIBING are ignored.
    bool recordingStreamActive_ = false;
};

}  // namespace echoflow

#endif
