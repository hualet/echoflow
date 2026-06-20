// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AsrEngine.h"

#include "SelfTest.h"
#include "log.h"

extern "C" {
#include <cblas.h>
#include "qwen_asr.h"
#include "qwen_asr_audio.h"
}

#include <chrono>
#include <cstdlib>
#include <memory>

namespace echoflow {

namespace {

struct LiveTokenCallbackState {
    std::string text;
    std::function<void(const std::string&)> callback;
    std::chrono::steady_clock::time_point started;
    std::chrono::steady_clock::time_point lastTokenAt;
    bool loggedFirstToken = false;
    int tokenCallbackCount = 0;
};

void liveTokenCallback(const char* piece, void* userdata)
{
    if (!piece || !userdata) {
        return;
    }

    auto* state = static_cast<LiveTokenCallbackState*>(userdata);
    state->text += piece;
    state->lastTokenAt = std::chrono::steady_clock::now();
    ++state->tokenCallbackCount;
    if (!state->loggedFirstToken) {
        const auto elapsed =
            std::chrono::duration<double>(state->lastTokenAt - state->started)
                .count();
        log("first live ASR token emitted after " + std::to_string(elapsed) + "s");
        state->loggedFirstToken = true;
    }
    try {
        state->callback(state->text);
    } catch (const std::exception& e) {
        log(std::string("live token callback failed: ") + e.what());
    } catch (...) {
        log("live token callback failed");
    }
}

class TokenCallbackScope {
public:
    TokenCallbackScope(qwen_ctx_t* ctx, qwen_token_cb callback, void* userdata)
        : ctx_(ctx)
    {
        qwen_set_token_callback(ctx_, callback, userdata);
    }

    ~TokenCallbackScope()
    {
        qwen_set_token_callback(ctx_, nullptr, nullptr);
    }

    TokenCallbackScope(const TokenCallbackScope&) = delete;
    TokenCallbackScope& operator=(const TokenCallbackScope&) = delete;

private:
    qwen_ctx_t* ctx_ = nullptr;
};

}  // namespace

AsrEngine::AsrEngine(Config cfg)
    : cfg_(std::move(cfg))
{
}

AsrEngine::~AsrEngine()
{
    if (ctx_) {
        qwen_free(ctx_);
    }
}

bool AsrEngine::ensureLoaded()
{
    if (ctx_) {
        return true;
    }

    auto modelDir = resolveModelDir(cfg_);
    if (cfg_.openBlasThreads > 0) {
        openblas_set_num_threads(cfg_.openBlasThreads);
    }
    log("loading qwen-asr model: " + modelDir.string());
    ctx_ = qwen_load(modelDir.c_str());
    if (!ctx_) {
        log("qwen_load failed: " + modelDir.string());
        return false;
    }

    if (cfg_.language.has_value() && !cfg_.language->empty()) {
        if (qwen_set_force_language(ctx_, cfg_.language->c_str()) != 0) {
            log("unsupported qwen-asr language, falling back to auto: " + *cfg_.language);
        }
    }
    if (!cfg_.prompt.empty()) {
        if (qwen_set_prompt(ctx_, cfg_.prompt.c_str()) != 0) {
            log("qwen_set_prompt failed");
        }
    }
    ctx_->skip_silence = cfg_.skipSilence ? 1 : 0;
    ctx_->stream_chunk_sec = 1.0f;
    ctx_->stream_unfixed_chunks = 0;
    ctx_->stream_rollback = 2;
    log("live stream params: chunk=1.0s, unfixed=0, rollback=2");
    return true;
}

bool AsrEngine::preload()
{
    return ensureLoaded();
}

std::string AsrEngine::transcribe(const std::filesystem::path& audio)
{
    if (!ensureLoaded()) {
        return {};
    }

    auto started = std::chrono::steady_clock::now();
    log("transcribing audio: " + audio.string());
    char* raw = nullptr;
    if (cfg_.streamTranscription) {
        int nSamples = 0;
        float* samples = qwen_load_wav(audio.c_str(), &nSamples);
        if (samples) {
            raw = qwen_transcribe_stream(ctx_, samples, nSamples);
            std::free(samples);
        }
    } else {
        raw = qwen_transcribe(ctx_, audio.c_str());
    }
    if (!raw) {
        log("qwen_transcribe returned empty result");
        return {};
    }

    std::string text(raw);
    std::free(raw);
    auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    log("transcription finished in " + std::to_string(elapsed) + "s, chars=" + std::to_string(text.size()));
    return text;
}

std::string AsrEngine::transcribeLive(
    void* liveAudio,
    std::function<void(const std::string&)> partialTextCallback)
{
    if (!liveAudio || !ensureLoaded()) {
        return {};
    }

    auto* live = static_cast<qwen_live_audio_t*>(liveAudio);
    auto started = std::chrono::steady_clock::now();
    log("transcribing live audio stream");
    LiveTokenCallbackState callbackState;
    callbackState.started = started;
    callbackState.callback = std::move(partialTextCallback);
    std::unique_ptr<TokenCallbackScope> callbackScope;
    if (callbackState.callback) {
        callbackScope =
            std::make_unique<TokenCallbackScope>(ctx_, liveTokenCallback, &callbackState);
    }
    char* raw = qwen_transcribe_stream_live(ctx_, live);
    const auto returnedAt = std::chrono::steady_clock::now();
    if (!raw) {
        log("qwen_transcribe_stream_live returned empty result");
        return {};
    }

    std::string text(raw);
    std::free(raw);
    auto elapsed = std::chrono::duration<double>(returnedAt - started).count();
    if (callbackState.loggedFirstToken) {
        const auto tailElapsed =
            std::chrono::duration<double>(returnedAt - callbackState.lastTokenAt).count();
        log("last live ASR token preceded return by " + std::to_string(tailElapsed) +
            "s, callbacks=" + std::to_string(callbackState.tokenCallbackCount));
    }
    log("live transcription finished in " + std::to_string(elapsed) + "s, chars=" + std::to_string(text.size()));
    return text;
}

}  // namespace echoflow
