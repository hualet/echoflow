// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CrispAsrEngine.h"

#include "CrispSession.h"
#include "log.h"

namespace echoflow {

std::string CrispAsrEngine::languageCode(const std::string& value)
{
    if (value == "Chinese" || value == "chinese" || value == "zh" || value == "cmn") {
        return "zh";
    }
    if (value == "English" || value == "english" || value == "en") {
        return "en";
    }
    return value;
}

CrispAsrEngine::CrispAsrEngine(Config cfg)
    : cfg_(std::move(cfg))
{
    session_ = std::make_unique<CrispSession>(cfg_.crispModelPath, cfg_.crispBackend,
                                              cfg_.crispThreads);
    if (session_->isLoaded()) {
        auto lang = languageCode(cfg_.language.value_or(""));
        if (!lang.empty()) {
            session_->setLanguage(lang);
        }
    }
}

CrispAsrEngine::~CrispAsrEngine() = default;

std::string CrispAsrEngine::transcribe(const std::filesystem::path& audio)
{
    if (!session_ || !session_->isLoaded()) {
        log("crisp session not loaded, cannot transcribe");
        return {};
    }

    auto pcm = CrispSession::readWavF32(audio.string());
    if (pcm.empty()) {
        log("failed to read audio for transcription: " + audio.string());
        return {};
    }

    std::string text = session_->transcribe(pcm.data(), static_cast<int>(pcm.size()));
    return text;
}

}  // namespace echoflow
