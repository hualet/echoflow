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
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

VoiceSession::ThreadLauncher defaultThreadLauncher()
{
    return [](std::function<void()> task) {
        return std::thread(std::move(task));
    };
}

} // namespace

VoiceSession::VoiceSession(Config cfg, IRecorder& recorder, IAsrEngine& asr,
    ICommitter& committer, IUiNotifier& ui, ThreadLauncher threadLauncher)
    : cfg_(std::move(cfg))
    , recorder_(&recorder)
    , asr_(&asr)
    , committer_(committer)
    , ui_(ui)
    , threadLauncher_(threadLauncher ? std::move(threadLauncher) : defaultThreadLauncher())
{
}

VoiceSession::VoiceSession(Config cfg, ILiveVoicePipeline& livePipeline,
    ICommitter& committer, IUiNotifier& ui, ThreadLauncher threadLauncher)
    : cfg_(std::move(cfg))
    , livePipeline_(&livePipeline)
    , committer_(committer)
    , ui_(ui)
    , threadLauncher_(threadLauncher ? std::move(threadLauncher) : defaultThreadLauncher())
    , partialCallbackState_(std::make_shared<PartialCallbackState>())
{
    partialCallbackState_->ui = &ui_;
    livePipeline_->setPartialTextCallback(
        [state = partialCallbackState_](const std::string& text) {
            IUiNotifier* notifier = nullptr;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (!text.empty() && state->accepting && state->ui) {
                    ++state->active;
                    notifier = state->ui;
                }
            }

            if (notifier) {
                const auto finishNotification = [&state] {
                    {
                        std::lock_guard<std::mutex> lock(state->mutex);
                        --state->active;
                    }
                    state->condition.notify_all();
                };
                try {
                    notifier->send("STREAM_TEXT " + text);
                } catch (...) {
                    finishNotification();
                    throw;
                }
                finishNotification();
            }
        });
}

VoiceSession::~VoiceSession()
{
    bool cancelRecording = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shuttingDown_ = true;
        if (state_ == SessionState::Recording) {
            cancelRecording = true;
            state_ = SessionState::Idle;
            runningGeneration_ = 0;
        } else if (state_ == SessionState::Transcribing || state_ == SessionState::Cancelling) {
            cancelRequested_ = true;
            state_ = SessionState::Cancelling;
        }
    }
    condition_.notify_all();

    // Close the finalization/commit gate before callback teardown can block.
    // No external collaborator is called while the session mutex is held.
    shutdownPartialTextCallback();

    if (cancelRecording) {
        try {
            if (livePipeline_) {
                livePipeline_->cancel();
            } else {
                recorder_->stop();
            }
        } catch (const std::exception& e) {
            log(std::string("voice cancel failed: ") + e.what());
        }
    }

    if (finalizationWorker_.joinable()) {
        finalizationWorker_.join();
    }
}

SessionState VoiceSession::state() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
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
        cancelActiveSession();
        return "TOOLTIP hide";
    }

    if (verb == "CANCEL") {
        // The capsule's X button: discard the recording and hide.
        return cancelActiveSession();
    }

    if (verb == "STATUS") {
        const auto phase = phaseName(state());
        ui_.send(phase);
        return phase;
    }

    if (verb == "CTRL_DOWN") {
        const auto currentState = state();
        if (currentState == SessionState::Idle) {
            return startRecording();
        }
        if (currentState == SessionState::Recording) {
            return startFinalization();
        }
        if (currentState == SessionState::Cancelling) {
            return "CANCELLING";
        }
        return "TRANSCRIBING";
    }

    return "ERR unknown-command";
}

std::string VoiceSession::startRecording()
{
    joinCompletedWorker();

    try {
        if (livePipeline_) {
            livePipeline_->start();
        } else {
            recorder_->start();
        }
    } catch (const std::exception& e) {
        log(std::string("voice start failed: ") + e.what());
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = SessionState::Idle;
            runningGeneration_ = 0;
            cancelRequested_ = false;
            commitClaimed_ = false;
            finalizationStartPublished_ = false;
        }
        ui_.send("IDLE");
        return "ERR " + std::string(e.what());
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        runningGeneration_ = ++nextGeneration_;
        cancelRequested_ = false;
        commitClaimed_ = false;
        finalizationStartPublished_ = false;
        state_ = SessionState::Recording;
    }
    ui_.send("RECORDING");
    setPartialTextAccepting(true);
    return "RECORDING";
}

std::string VoiceSession::startFinalization()
{
    std::uint64_t generation = 0;
    std::string launchError;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != SessionState::Recording) {
            return phaseName(state_);
        }

        // The pipeline is about to be finalized; stop accepting streaming
        // partials. The worker waits for TRANSCRIBING to be published before
        // touching capture/ASR, so notifier calls stay outside this mutex.
        state_ = SessionState::Transcribing;
        finalizationRunning_ = true;
        finalizationStartPublished_ = false;
        commitClaimed_ = false;
        generation = runningGeneration_;
        try {
            finalizationWorker_
                = threadLauncher_([this, generation] { finalize(generation); });
        } catch (const std::system_error& e) {
            launchError = e.what();
            state_ = SessionState::Recording;
            finalizationRunning_ = false;
            finalizationStartPublished_ = false;
        }
    }

    if (!launchError.empty()) {
        log("voice finalization launch failed: " + launchError);
        ui_.send("RECORDING");
        return "ERR " + launchError;
    }

    setPartialTextAccepting(false);
    ui_.send("TRANSCRIBING");
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (finalizationRunning_ && runningGeneration_ == generation) {
            finalizationStartPublished_ = true;
        }
    }
    condition_.notify_all();
    return "TRANSCRIBING";
}

void VoiceSession::finalize(std::uint64_t generation)
{
    {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this, generation] {
            return shuttingDown_ || (finalizationRunning_
                && runningGeneration_ == generation && finalizationStartPublished_);
        });
        if (shuttingDown_ && !finalizationStartPublished_) {
            return;
        }
    }

    std::string text;
    if (livePipeline_) {
        try {
            text = livePipeline_->finish();
        } catch (const std::exception& e) {
            log(std::string("asr transcribe failed: ") + e.what());
        }
    } else {
        try {
            const auto audio = recorder_->stop();
            if (!audio.empty()) {
                text = asr_->transcribe(audio);
            }
        } catch (const std::exception& e) {
            log(std::string("asr transcribe failed: ") + e.what());
        }
    }
    if (cfg_.stripTrailingPunctuation) {
        text = stripPunctuation(text);
    }

    bool commitClaimed = false;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        const bool current = finalizationRunning_ && runningGeneration_ == generation;

        // This is the cancellation/commit claim gate. CANCEL taking it first
        // suppresses the external commit. A worker taking it first records an
        // irrevocable claim, then releases the mutex before commitText().
        if (current && !cancelRequested_ && !shuttingDown_ && !text.empty()) {
            commitClaimed_ = true;
            commitClaimed = true;
        }
    }

    if (commitClaimed) {
        try {
            const auto [ok, detail] = committer_.commitText(text);
            if (!ok) {
                log("voice commit failed: " + detail);
            }
        } catch (const std::exception& e) {
            log(std::string("voice commit failed: ") + e.what());
        }
    }

    bool notifyIdle = false;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        const bool current = finalizationRunning_ && runningGeneration_ == generation;
        if (current) {
            // An accepted CANCEL increments the pending count before its
            // notifier call. Waiting here releases mutex_, guaranteeing
            // CANCELLING is published before the terminal IDLE.
            condition_.wait(lock,
                [this] { return pendingCancellationNotifications_ == 0; });
            state_ = SessionState::Idle;
            runningGeneration_ = 0;
            finalizationRunning_ = false;
            finalizationStartPublished_ = false;
            commitClaimed_ = false;
            cancelRequested_ = false;
            notifyIdle = !shuttingDown_;
        }
    }
    if (notifyIdle) {
        ui_.send("IDLE");
    }
}

void VoiceSession::joinCompletedWorker()
{
    std::thread completedWorker;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (finalizationWorker_.joinable() && !finalizationRunning_) {
            completedWorker = std::move(finalizationWorker_);
        }
    }
    if (completedWorker.joinable()) {
        completedWorker.join();
    }
}

void VoiceSession::setPartialTextAccepting(bool accepting)
{
    if (!partialCallbackState_) {
        return;
    }

    const auto state = partialCallbackState_;
    std::unique_lock<std::mutex> lock(state->mutex);
    state->accepting = accepting;
    if (!accepting) {
        state->condition.wait(lock, [state] { return state->active == 0; });
    }
}

void VoiceSession::shutdownPartialTextCallback()
{
    if (!partialCallbackState_) {
        return;
    }

    const auto state = partialCallbackState_;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->accepting = false;
    }
    livePipeline_->setPartialTextCallback({});
    {
        std::unique_lock<std::mutex> lock(state->mutex);
        state->condition.wait(lock, [state] { return state->active == 0; });
        state->ui = nullptr;
    }
}

std::string VoiceSession::cancelActiveSession()
{
    bool cancelRecording = false;
    bool publishCancellation = false;
    std::string result = "CANCELLED";
    std::string notification;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == SessionState::Recording) {
            cancelRecording = true;
            state_ = SessionState::Idle;
            runningGeneration_ = 0;
            cancelRequested_ = false;
            notification = "IDLE";
        } else if (state_ == SessionState::Transcribing && commitClaimed_) {
            // The external commit has already won the claim gate. Report the
            // in-flight phase honestly; cancellation can no longer suppress it.
            result = "TRANSCRIBING";
        } else if (state_ == SessionState::Transcribing || state_ == SessionState::Cancelling) {
            cancelRequested_ = true;
            state_ = SessionState::Cancelling;
            ++pendingCancellationNotifications_;
            publishCancellation = true;
        } else {
            notification = "IDLE";
        }
    }

    if (cancelRecording) {
        // No phase after Recording may overtake an already-admitted partial.
        // This wait uses only the callback-state mutex and releases it while
        // the external notifier completes.
        setPartialTextAccepting(false);
    }

    if (publishCancellation) {
        const auto finishNotification = [this] {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                --pendingCancellationNotifications_;
            }
            condition_.notify_all();
        };
        try {
            ui_.send("CANCELLING");
        } catch (...) {
            finishNotification();
            throw;
        }
        finishNotification();
    }

    if (cancelRecording) {
        if (livePipeline_) {
            try {
                livePipeline_->cancel();
            } catch (const std::exception& e) {
                log(std::string("voice cancel failed: ") + e.what());
            }
        } else {
            try {
                recorder_->stop();
            } catch (const std::exception& e) {
                log(std::string("voice cancel failed: ") + e.what());
            }
        }
    }
    if (!notification.empty()) {
        ui_.send(notification);
    }
    return result;
}

std::string VoiceSession::phaseName(SessionState state)
{
    switch (state) {
    case SessionState::Idle:
        return "IDLE";
    case SessionState::Recording:
        return "RECORDING";
    case SessionState::Transcribing:
        return "TRANSCRIBING";
    case SessionState::Cancelling:
        return "CANCELLING";
    }
    return "IDLE";
}

} // namespace echoflow
