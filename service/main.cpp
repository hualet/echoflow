// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Committer.h"
#include "AudioSegmenter.h"
#include "Config.h"
#include "CrispAsrEngine.h"
#include "CrispLiveVoicePipeline.h"
#include "LocalLlmPolisher.h"
#include "Recorder.h"
#include "SelfTest.h"
#include "Server.h"
#include "UiNotifier.h"
#include "VoiceSession.h"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path defaultConfigPath()
{
    if (const char* configHome = std::getenv("XDG_CONFIG_HOME")) {
        return fs::path(configHome) / "echoflow" / "echoflow.conf";
    }
    const char* home = std::getenv("HOME");
    return fs::path(home ? home : "/tmp") / ".config" / "echoflow" / "echoflow.conf";
}

void printUsage()
{
    std::printf("Usage: echoflow-service [--config PATH] [--self-test] "
                "[--print-default-config] [--transcribe-file FILE] "
                "[--polish-text TEXT]\n");
}

void printDefaultConfig()
{
    auto cfg = echoflow::Config::defaultConfig();
    const echoflow::AudioSegmenterConfig segmenter;
    std::printf("{\n"
                "  \"model_name\": \"%s\",\n"
                "  \"model_dir\": \"(derived at runtime: <config_dir>/<model_name>)\",\n"
                "  \"language\": \"%s\",\n"
                "  \"prompt\": \"%s\",\n"
                "  \"recordings_dir\": \"%s\",\n"
                "  \"min_record_seconds\": %.2f,\n"
                "  \"rate\": %d,\n"
                "  \"channels\": %d,\n"
                "  \"format\": \"%s\",\n"
                "  \"source\": \"%s\",\n"
                 "  \"stream_transcription\": %s,\n"
                 "  \"save_live_debug_audio\": %s,\n"
                 "  \"openblas_threads\": %d,\n"
                 "  \"fcitx_commit\": %s,\n"
                 "  \"crisp_model_path\": \"%s\",\n"
                 "  \"crisp_backend\": \"%s\",\n"
                 "  \"crisp_threads\": %d,\n"
                 "  \"crisp_max_new_tokens\": %d,\n"
                 "  \"vad_backend\": \"%s\",\n"
                 "  \"vad_model_path\": \"%s\",\n"
                 "  \"vad_threshold\": %.2f,\n"
                 "  \"vad_end_threshold\": %.2f,\n"
                 "  \"vad_min_speech_ms\": %d,\n"
                 "  \"vad_min_silence_ms\": %d,\n"
                 "  \"polish_enabled\": %s,\n"
                 "  \"polish_model_name\": \"%s\",\n"
                 "  \"energy_speech_ratio\": %.1f,\n"
                 "  \"energy_min_speech_rms\": %.1f,\n"
                 "  \"forced_segment_overlap_ms\": %d\n"
                 "}\n",
                 cfg.modelName.c_str(),
                 cfg.language.value_or("").c_str(), cfg.prompt.c_str(),
                 cfg.recordingsDir.c_str(), cfg.minRecordSeconds,
                 cfg.pwRecord.rate, cfg.pwRecord.channels, cfg.pwRecord.format.c_str(),
                 cfg.pwRecord.source.c_str(),
                 cfg.streamTranscription ? "true" : "false",
                 cfg.saveLiveDebugAudio ? "true" : "false",
                 cfg.openBlasThreads, cfg.fcitxCommit ? "true" : "false",
                 cfg.crispModelPath.c_str(), cfg.crispBackend.c_str(),
                 cfg.crispThreads, cfg.crispMaxNewTokens,
                 cfg.vadBackend.c_str(), cfg.vadModelPath.c_str(),
                 static_cast<double>(cfg.vadThreshold),
                 static_cast<double>(cfg.vadEndThreshold),
                 cfg.vadMinSpeechMs, cfg.vadMinSilenceMs,
                 cfg.polishEnabled ? "true" : "false", cfg.polishModelName.c_str(),
                 segmenter.speechRatio, segmenter.minSpeechRms,
                 segmenter.forceOverlapMs);
}

std::string polishOrRaw(echoflow::ITextPolisher* polisher, const std::string& text)
{
    if (!polisher || text.empty()) {
        return text;
    }
    try {
        return polisher->polish(text, {});
    } catch (const std::exception& e) {
        std::fprintf(stderr, "text polish failed: %s\n", e.what());
        return text;
    }
}

}  // namespace

int main(int argc, char** argv)
{
    fs::path configPath = defaultConfigPath();
    bool selfTest = false;
    bool printDefault = false;
    std::optional<fs::path> transcribeFile;
    std::optional<std::string> polishInput;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            configPath = argv[++i];
        } else if (arg == "--self-test") {
            selfTest = true;
        } else if (arg == "--print-default-config") {
            printDefault = true;
        } else if (arg == "--transcribe-file" && i + 1 < argc) {
            transcribeFile = argv[++i];
        } else if (arg == "--polish-text" && i + 1 < argc) {
            polishInput = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
            printUsage();
            return 2;
        }
    }

    if (printDefault) {
        printDefaultConfig();
        return 0;
    }

    echoflow::Config cfg;
    if (fs::exists(configPath)) {
        cfg = echoflow::loadDtkConf(configPath);
    } else {
        cfg = echoflow::Config::defaultConfig();
        cfg.modelDir =
            (configPath.parent_path() / echoflow::normalizeModelName(cfg.modelName)).string();
        cfg.polishModelPath =
            echoflow::polishModelPathFor(configPath.parent_path(), cfg.polishModelName);
    }

    if (selfTest) {
        return echoflow::runSelfTest(cfg);
    }

    if (polishInput.has_value()) {
        // Runs the configured polish model even when the feature is off, so
        // a downloaded model can be checked before enabling it.
        // "-" reads one transcript per stdin line through a single session,
        // which shows warm latency once the prompt prefix is cached.
        echoflow::LocalLlmPolisher polisher(cfg.polishModelPath, cfg.crispThreads, true);
        polisher.preload();
        std::vector<std::string> inputs;
        if (*polishInput == "-") {
            for (std::string line; std::getline(std::cin, line);) {
                if (!line.empty()) {
                    inputs.push_back(line);
                }
            }
        } else {
            inputs.push_back(*polishInput);
        }
        int status = 0;
        for (const auto& input : inputs) {
            try {
                std::printf("%s\n", polisher.polish(input, {}).c_str());
            } catch (const std::exception& e) {
                std::fprintf(stderr, "text polish failed: %s\n", e.what());
                status = 1;
            }
            std::fflush(stdout);
        }
        return status;
    }

    std::unique_ptr<echoflow::LocalLlmPolisher> polisher;
    if (cfg.polishEnabled) {
        if (cfg.polishModelPath.empty()) {
            std::fprintf(stderr, "unknown polish model: %s\n", cfg.polishModelName.c_str());
        } else {
            polisher = std::make_unique<echoflow::LocalLlmPolisher>(
                cfg.polishModelPath, cfg.crispThreads, cfg.logTranscripts);
        }
    }

    if (transcribeFile.has_value()) {
        echoflow::CrispAsrEngine asr(cfg);
        std::string text = polishOrRaw(polisher.get(), asr.transcribe(*transcribeFile));
        if (cfg.stripTrailingPunctuation) {
            text = echoflow::stripPunctuation(text);
        }
        std::printf("%s\n", text.c_str());
        return text.empty() ? 1 : 0;
    }

    echoflow::Committer committer(cfg, echoflow::fcitxSocketPath(cfg));
    echoflow::UnixDatagramUiNotifier ui(echoflow::uiSocketPath(cfg));
    if (polisher) {
        polisher->preload();
    }
    if (cfg.streamTranscription) {
        echoflow::CrispLiveVoicePipeline livePipeline(cfg);
        echoflow::VoiceSession session(cfg, livePipeline, committer, ui);
        session.setTextPolisher(polisher.get());
        echoflow::Server server(cfg, session);
        return server.run();
    }

    echoflow::PipeWireRecorder recorder(cfg);
    echoflow::CrispAsrEngine asr(cfg);
    echoflow::VoiceSession session(cfg, recorder, asr, committer, ui);
    session.setTextPolisher(polisher.get());
    echoflow::Server server(cfg, session);
    return server.run();
}
