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

    std::string handleCommand(const std::string& command);
    SessionState state() const { return state_; }
    bool tooltipVisible() const { return tooltipVisible_; }
    bool typedHidden() const { return typedHidden_; }

private:
    std::string startRecording();
    std::string stopTranscribeCommit();

    Config cfg_;
    IRecorder& recorder_;
    IAsrEngine& asr_;
    ICommitter& committer_;
    IUiNotifier& ui_;
    SessionState state_ = SessionState::Idle;
    bool tooltipVisible_ = false;
    bool typedHidden_ = false;
};

}  // namespace echoflow

#endif
