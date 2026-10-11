// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_CONFIG_H
#define ECHOFLOW_CONFIG_H

#include <filesystem>
#include <optional>
#include <string>

namespace echoflow {

// CrispASR CPU threads for a host with the given physical core count. SMT
// siblings and threads beyond eight slow qwen3 encoder/prefill on measured
// hosts, so use one thread per physical core up to that cap.
int crispThreadsForPhysicalCores(int physicalCores);
int defaultCrispThreads();

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
    int crispThreads = defaultCrispThreads();
    int crispMaxNewTokens = 0;
    std::string vadBackend = "energy";
    std::string vadModelPath;
    float vadThreshold = 0.15f;
    float vadEndThreshold = 0.10f;
    int vadMinSpeechMs = 100;
    int vadMinSilenceMs = 300;

    // Optional rewrite of the final transcript by a local chat model before
    // it is committed. The model path is derived like the ASR model path.
    bool polishEnabled = false;
    std::string polishModelName = "qwen3-0.6b";
    std::string polishModelPath;

    static Config defaultConfig();
};

Config loadDtkConf(const std::filesystem::path& path);
std::string normalizeModelName(const std::string& value);
// <configDir>/<id>/<gguf> for a polish catalog id, or empty when unknown.
std::string polishModelPathFor(const std::filesystem::path& configDir, const std::string& id);
std::string expandPath(const std::string& value, const std::filesystem::path& baseDir);
std::filesystem::path runtimeDir();
std::filesystem::path controlSocketPath(const Config& cfg);
std::filesystem::path fcitxSocketPath(const Config& cfg);
std::filesystem::path uiSocketPath(const Config& cfg);
std::string stripPunctuation(const std::string& text);

}  // namespace echoflow

#endif
