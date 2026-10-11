// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "LocalLlmPolisher.h"

#include "PolishPrompt.h"
#include "log.h"

#include "crispasr_chat.h"
#include "llama.h"

#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <utility>
#include <vector>

namespace echoflow {

namespace {

// System prompt, few-shot turns and a long dictation fit comfortably; a
// larger window only costs KV memory.
constexpr int kContextTokens = 2048;

// llama.cpp dumps every GGUF key and tensor at load; keep the service log
// readable by passing only errors through.
void forwardErrors(ggml_log_level level, const char* text, void*)
{
    if (level == GGML_LOG_LEVEL_ERROR) {
        std::fputs(text, stderr);
    }
}

std::vector<crispasr_chat_message> chatMessages(const std::vector<PolishMessage>& messages)
{
    std::vector<crispasr_chat_message> out;
    out.reserve(messages.size());
    for (const auto& message : messages) {
        out.push_back({ message.role.c_str(), message.content.c_str() });
    }
    return out;
}

}  // namespace

LocalLlmPolisher::LocalLlmPolisher(std::string modelPath, int threads)
    : modelPath_(std::move(modelPath))
    , threads_(threads)
{
}

LocalLlmPolisher::~LocalLlmPolisher()
{
    if (preloadThread_.joinable()) {
        preloadThread_.join();
    }
    crispasr_chat_close(session_);
}

void LocalLlmPolisher::preload()
{
    if (preloadThread_.joinable()) {
        return;
    }
    preloadThread_ = std::thread([this] {
        std::lock_guard<std::mutex> lock(mutex_);
        ensureLoadedLocked();
        warmUpLocked();
    });
}

void LocalLlmPolisher::ensureLoadedLocked()
{
    if (loadAttempted_) {
        return;
    }
    loadAttempted_ = true;

    const auto started = std::chrono::steady_clock::now();
    crispasr_chat_open_params params;
    crispasr_chat_open_params_default(&params);
    params.n_threads = threads_;
    params.n_threads_batch = threads_;
    params.n_ctx = kContextTokens;
    params.n_gpu_layers = 0;

    llama_log_set(forwardErrors, nullptr);
    crispasr_chat_error err {};
    session_ = crispasr_chat_open(modelPath_.c_str(), &params, &err);
    if (!session_) {
        loadError_ = err.message[0] != '\0' ? err.message : "failed to open model";
        log("polish model load failed: " + modelPath_ + ": " + loadError_);
        return;
    }
    crispasr_chat_set_abort_callback(session_, &LocalLlmPolisher::continueGeneration, this);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    log("polish model loaded in " + std::to_string(elapsed.count()) + " ms: " + modelPath_);
}

void LocalLlmPolisher::warmUpLocked()
{
    if (!session_) {
        return;
    }
    // Prefill the fixed system/few-shot prefix so the first dictation only
    // decodes its own transcript; the session keeps the shared KV prefix.
    const auto started = std::chrono::steady_clock::now();
    const auto polishMessages = buildPolishMessages(std::string());
    const auto messages = chatMessages(polishMessages);
    crispasr_chat_generate_params params;
    crispasr_chat_generate_params_default(&params);
    params.prefill_only = true;
    crispasr_chat_error err {};
    char* reply = crispasr_chat_generate(
        session_, messages.data(), messages.size(), &params, &err);
    crispasr_chat_string_free(reply);
    if (err.code != 0) {
        log(std::string("polish warm-up failed: ") + err.message);
        return;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    log("polish prompt prefilled in " + std::to_string(elapsed.count()) + " ms");
}

bool LocalLlmPolisher::continueGeneration(void* user)
{
    const auto* self = static_cast<LocalLlmPolisher*>(user);
    return !(self->shouldAbort_ && *self->shouldAbort_ && (*self->shouldAbort_)());
}

std::string LocalLlmPolisher::polish(const std::string& text,
    const std::function<bool()>& shouldAbort)
{
    std::lock_guard<std::mutex> lock(mutex_);
    ensureLoadedLocked();
    if (!session_) {
        throw std::runtime_error("polish model unavailable: " + loadError_);
    }

    const auto polishMessages = buildPolishMessages(text);
    const auto messages = chatMessages(polishMessages);

    crispasr_chat_generate_params params;
    crispasr_chat_generate_params_default(&params);
    params.max_tokens = polishMaxTokens(text);
    params.temperature = 0.0f;
    // A repeat penalty fights the faithful copy this task mostly is.
    params.repeat_penalty = 1.0f;

    struct AbortScope {
        const std::function<bool()>*& slot;
        ~AbortScope() { slot = nullptr; }
    } abortScope { shouldAbort_ };
    shouldAbort_ = &shouldAbort;

    const auto started = std::chrono::steady_clock::now();
    crispasr_chat_error err {};
    char* reply = crispasr_chat_generate(
        session_, messages.data(), messages.size(), &params, &err);
    if (!reply) {
        if (err.code == CRISPASR_CHAT_ERR_ABORTED) {
            throw std::runtime_error("aborted");
        }
        throw std::runtime_error(err.message[0] != '\0' ? err.message : "generation failed");
    }
    const std::string raw(reply);
    crispasr_chat_string_free(reply);

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    log("polish generated in " + std::to_string(elapsed.count()) + " ms");

    std::string polished = cleanPolishedText(raw);
    if (!isPlausiblePolish(text, polished)) {
        throw std::runtime_error("implausible rewrite rejected");
    }
    return polished;
}

}  // namespace echoflow
