// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AsrEngine.h"
#include "Committer.h"
#include "Config.h"
#include "PipeWireLiveVoicePipeline.h"
#include "Recorder.h"
#include "SelfTest.h"
#include "Server.h"
#include "UiNotifier.h"
#include "VoiceSession.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

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
                "[--print-default-config] [--transcribe-file FILE]\n");
}

void printDefaultConfig()
{
    auto cfg = echoflow::Config::defaultConfig();
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
                "  \"fcitx_commit\": %s\n"
                "}\n",
                cfg.modelName.c_str(),
                cfg.language.value_or("").c_str(), cfg.prompt.c_str(),
                cfg.recordingsDir.c_str(), cfg.minRecordSeconds,
                cfg.pwRecord.rate, cfg.pwRecord.channels, cfg.pwRecord.format.c_str(),
                cfg.pwRecord.source.c_str(),
                cfg.streamTranscription ? "true" : "false",
                cfg.saveLiveDebugAudio ? "true" : "false",
                cfg.openBlasThreads, cfg.fcitxCommit ? "true" : "false");
}

}  // namespace

int main(int argc, char** argv)
{
    fs::path configPath = defaultConfigPath();
    bool selfTest = false;
    bool printDefault = false;
    std::optional<fs::path> transcribeFile;

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
    }

    if (selfTest) {
        return echoflow::runSelfTest(cfg);
    }

    if (transcribeFile.has_value()) {
        echoflow::AsrEngine asr(cfg);
        std::string text = asr.transcribe(*transcribeFile);
        if (cfg.stripTrailingPunctuation) {
            text = echoflow::stripPunctuation(text);
        }
        std::printf("%s\n", text.c_str());
        return text.empty() ? 1 : 0;
    }

    echoflow::AsrEngine asr(cfg);
    asr.preload();
    echoflow::Committer committer(cfg, echoflow::fcitxSocketPath(cfg));
    echoflow::UnixDatagramUiNotifier ui(echoflow::uiSocketPath(cfg));
    if (cfg.streamTranscription) {
        echoflow::PipeWireLiveVoicePipeline livePipeline(cfg, asr);
        echoflow::VoiceSession session(cfg, livePipeline, committer, ui);
        echoflow::Server server(cfg, session);
        return server.run();
    }

    echoflow::PipeWireRecorder recorder(cfg);
    echoflow::VoiceSession session(cfg, recorder, asr, committer, ui);
    echoflow::Server server(cfg, session);
    return server.run();
}
