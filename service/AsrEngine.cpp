// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AsrEngine.h"

#include "SelfTest.h"
#include "log.h"

extern "C" {
#include <cblas.h>
#include "qwen_asr_audio.h"
}

#include <chrono>
#include <cstdlib>

namespace echoflow {

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

}  // namespace echoflow
