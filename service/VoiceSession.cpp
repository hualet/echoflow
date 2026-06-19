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
}

std::string VoiceSession::handleCommand(const std::string& command)
{
    std::string cmd = trim(command);
    auto space = cmd.find(' ');
    std::string verb = upper(space == std::string::npos ? cmd : cmd.substr(0, space));
    std::string argument = trim(space == std::string::npos ? std::string() : cmd.substr(space + 1));

    if (verb == "FOCUS") {
        tooltipVisible_ = true;
        if (typedHidden_) {
            return "TOOLTIP suppressed";
        }
        std::string suffix = argument.empty() ? std::string() : " " + argument;
        ui_.send("SHOW_TOOLTIP" + suffix + " 按右 Ctrl 语音输入");
        return "TOOLTIP show";
    }

    if (verb == "BLUR") {
        tooltipVisible_ = false;
        typedHidden_ = false;
        if (state_ == SessionState::Recording) {
            if (livePipeline_) {
                livePipeline_->cancel();
            } else {
                recorder_->stop();
            }
        }
        state_ = SessionState::Idle;
        ui_.send("HIDE_TOOLTIP");
        return "TOOLTIP hide";
    }

    if (verb == "TYPED") {
        if (state_ == SessionState::Idle && tooltipVisible_ && !typedHidden_) {
            typedHidden_ = true;
            ui_.send("HIDE_TOOLTIP");
            return "TYPING hide";
        }
        return "IGNORED";
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
        ui_.send("IDLE");
        return "ERR " + std::string(e.what());
    }
    ui_.send("RECORDING");
    state_ = SessionState::Recording;
    return "RECORDING";
}

std::string VoiceSession::stopTranscribeCommit()
{
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

}  // namespace echoflow
