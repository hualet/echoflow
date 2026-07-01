// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "VoiceSession.h"

#include "log.h"

#include <algorithm>
#include <cctype>
#include <exception>

namespace echoflow {

namespace {

std::string trim(const std::string& value)
{
    auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string upper(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

}  // namespace

VoiceSession::VoiceSession(Config cfg, IRecorder& recorder, IAsrEngine& asr,
                           ICommitter& committer, IUiNotifier& ui)
    : cfg_(std::move(cfg))
    , recorder_(&recorder)
    , asr_(&asr)
    , committer_(committer)
    , ui_(ui)
{
}

VoiceSession::VoiceSession(Config cfg, ILiveVoicePipeline& livePipeline,
                           ICommitter& committer, IUiNotifier& ui)
    : cfg_(std::move(cfg))
    , livePipeline_(&livePipeline)
    , committer_(committer)
    , ui_(ui)
{
    livePipeline_->setPartialTextCallback([this](const std::string& text) {
        // Ignore partials that arrive after the pipeline was finished or
        // cancelled — they belong to a session the user already closed.
        if (!text.empty() && recordingStreamActive_) {
            ui_.send("STREAM_TEXT " + text);
        }
    });
}

std::string VoiceSession::handleCommand(const std::string& command)
{
    std::string cmd = trim(command);
    auto space = cmd.find(' ');
    std::string verb = upper(space == std::string::npos ? cmd : cmd.substr(0, space));
    std::string argument = trim(space == std::string::npos ? std::string() : cmd.substr(space + 1));

    // The capsule no longer tracks input focus, so FOCUS/TYPED no longer show
    // or hide anything. We keep accepting them for backward compatibility with
    // older fcitx addons; they are now harmless no-ops.
    if (verb == "FOCUS" || verb == "TYPED") {
        return "IGNORED";
    }

    if (verb == "BLUR") {
        // Focus left the input context: discard any live recording the same way
        // the X button does, then hide the capsule.
        cancelRecording();
        return "TOOLTIP hide";
    }

    if (verb == "CANCEL") {
        // The capsule's X button: discard the recording and hide.
        cancelRecording();
        return "CANCELLED";
    }

    if (verb == "CTRL_DOWN") {
        if (state_ == SessionState::Idle) {
            return startRecording();
        }
        if (state_ == SessionState::Recording) {
            return stopTranscribeCommit();
        }
        return "TRANSCRIBING";
    }

    return "ERR unknown-command";
}

std::string VoiceSession::startRecording()
{
    try {
        if (livePipeline_) {
            livePipeline_->start();
        } else {
            recorder_->start();
        }
    } catch (const std::exception& e) {
        log(std::string("voice start failed: ") + e.what());
        state_ = SessionState::Idle;
        recordingStreamActive_ = false;
        ui_.send("IDLE");
        return "ERR " + std::string(e.what());
    }
    recordingStreamActive_ = true;
    ui_.send("RECORDING");
    state_ = SessionState::Recording;
    return "RECORDING";
}

std::string VoiceSession::stopTranscribeCommit()
{
    // The pipeline is about to be finalized; stop accepting streaming partials.
    recordingStreamActive_ = false;
    state_ = SessionState::Transcribing;
    ui_.send("TRANSCRIBING");

    std::string text;
    if (livePipeline_) {
        try {
            text = livePipeline_->finish();
        } catch (const std::exception& e) {
            log(std::string("asr transcribe failed: ") + e.what());
        }
    } else {
        auto audio = recorder_->stop();
        if (audio.empty()) {
            state_ = SessionState::Idle;
            ui_.send("IDLE");
            return "CANCELLED";
        }

        try {
            text = asr_->transcribe(audio);
        } catch (const std::exception& e) {
            log(std::string("asr transcribe failed: ") + e.what());
        }
    }
    if (cfg_.stripTrailingPunctuation) {
        text = stripPunctuation(text);
    }
    if (text.empty()) {
        state_ = SessionState::Idle;
        ui_.send("IDLE");
        return "EMPTY";
    }

    auto [ok, detail] = committer_.commitText(text);
    state_ = SessionState::Idle;
    ui_.send("IDLE");
    return ok ? "COMMITTED" : "ERR " + detail;
}

void VoiceSession::cancelRecording()
{
    recordingStreamActive_ = false;
    if (state_ == SessionState::Recording) {
        if (livePipeline_) {
            try {
                livePipeline_->cancel();
            } catch (const std::exception& e) {
                log(std::string("voice cancel failed: ") + e.what());
            }
        } else {
            recorder_->stop();
        }
    }
    state_ = SessionState::Idle;
    ui_.send("IDLE");
}

}  // namespace echoflow
