// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_CONFIG_H
#define ECHOFLOW_CONFIG_H

#include <filesystem>
#include <optional>
#include <string>

namespace echoflow {

struct PipeWireRecordConfig {
    int rate = 16000;
    int channels = 1;
    std::string format = "s16";
    std::string source;
};

struct Config {
    std::string recordingsDir;
    std::string modelDir;
    std::string modelName = "qwen3-asr-0.6b";
    std::optional<std::string> language = "Chinese";
    std::string prompt;
    int asrTimeoutSeconds = 120;
    double minRecordSeconds = 0.25;
    PipeWireRecordConfig pwRecord;
    bool fcitxCommit = true;
    bool stripTrailingPunctuation = false;
    bool skipSilence = false;
    bool streamTranscription = true;
    bool saveLiveDebugAudio = false;
    int openBlasThreads = 4;

    std::string crispModelPath;
    std::string crispBackend = "qwen3";
    int crispThreads = 6;
    int crispMaxNewTokens = 0;

    static Config defaultConfig();
};

Config loadDtkConf(const std::filesystem::path& path);
std::string normalizeModelName(const std::string& value);
std::string expandPath(const std::string& value, const std::filesystem::path& baseDir);
std::filesystem::path runtimeDir();
std::filesystem::path controlSocketPath(const Config& cfg);
std::filesystem::path fcitxSocketPath(const Config& cfg);
std::filesystem::path uiSocketPath(const Config& cfg);
std::string stripPunctuation(const std::string& text);

}  // namespace echoflow

#endif
