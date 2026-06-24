// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CrispAsrEngine.h"
#include "Config.h"
#include "Interfaces.h"
#include "VoiceSession.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

namespace fs = std::filesystem;

namespace {

using Clock = std::chrono::steady_clock;

double elapsedMs(Clock::time_point started)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}

std::string jsonEscape(const std::string& value)
{
    std::string out;
    out.reserve(value.size() + 8);
    for (char c : value) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

fs::path defaultConfigPath()
{
    if (const char* configHome = std::getenv("XDG_CONFIG_HOME")) {
        return fs::path(configHome) / "echoflow" / "echoflow.conf";
    }
    const char* home = std::getenv("HOME");
    return fs::path(home ? home : "/tmp") / ".config" / "echoflow" / "echoflow.conf";
}

echoflow::Config loadConfig(const std::optional<fs::path>& path,
                            const std::optional<fs::path>& modelDir)
{
    fs::path configPath = path.value_or(defaultConfigPath());
    echoflow::Config cfg;
    if (fs::exists(configPath)) {
        cfg = echoflow::loadDtkConf(configPath);
    } else {
        cfg = echoflow::Config::defaultConfig();
        cfg.modelDir =
            (configPath.parent_path() / echoflow::normalizeModelName(cfg.modelName)).string();
    }
    if (modelDir.has_value()) {
        cfg.modelDir = modelDir->string();
    }
    return cfg;
}

class TimedRecorder : public echoflow::IRecorder {
public:
    double startMs = 0.0;
    double stopMs = 0.0;
    fs::path path = "/tmp/echoflow-synthetic.wav";

    void start() override
    {
        auto started = Clock::now();
        startMs = elapsedMs(started);
    }

    fs::path stop() override
    {
        auto started = Clock::now();
        stopMs = elapsedMs(started);
        return path;
    }
};

class TimedAsr : public echoflow::IAsrEngine {
public:
    double transcribeMs = 0.0;
    std::string text = "synthetic voice text";

    std::string transcribe(const fs::path&) override
    {
        auto started = Clock::now();
        transcribeMs = elapsedMs(started);
        return text;
    }
};

class TimedCommitter : public echoflow::ICommitter {
public:
    double commitMs = 0.0;
    std::string lastText;

    std::pair<bool, std::string> commitText(const std::string& text) override
    {
        auto started = Clock::now();
        lastText = text;
        commitMs = elapsedMs(started);
        return {true, "OK"};
    }
};

class CountingUi : public echoflow::IUiNotifier {
public:
    int messages = 0;
    void send(const std::string&) override { ++messages; }
};

void printUsage()
{
    std::fprintf(stderr,
                 "Usage:\n"
                 "  voice_latency_benchmark --session-synthetic [--iterations N]\n"
                 "  voice_latency_benchmark --transcribe-file FILE [--config PATH] "
                 "[--model-dir PATH] [--threads N] [--openblas-threads N] "
                 "[--skip-silence] [--stream] [--iterations N] [--no-preload]\n");
}

int runSynthetic(int iterations)
{
    for (int i = 0; i < iterations; ++i) {
        TimedRecorder recorder;
        TimedAsr asr;
        TimedCommitter committer;
        CountingUi ui;
        echoflow::VoiceSession session(echoflow::Config::defaultConfig(), recorder, asr, committer, ui);

        session.handleCommand("CTRL_DOWN");
        auto started = Clock::now();
        std::string reply = session.handleCommand("CTRL_DOWN");
        double stopToReplyMs = elapsedMs(started);

        std::printf("{\"mode\":\"session-synthetic\",\"iteration\":%d,"
                    "\"stop_to_reply_ms\":%.3f,\"record_stop_ms\":%.3f,"
                    "\"transcribe_ms\":%.3f,\"commit_ms\":%.3f,"
                    "\"ui_messages\":%d,\"reply\":\"%s\"}\n",
                    i + 1, stopToReplyMs, recorder.stopMs, asr.transcribeMs,
                    committer.commitMs, ui.messages, jsonEscape(reply).c_str());
    }
    return 0;
}

int runTranscribeFile(const fs::path& audio, const std::optional<fs::path>& configPath,
                      const std::optional<fs::path>& modelDir, bool /*preload*/,
                      int /*threads*/, int /*openBlasThreads*/, bool /*skipSilence*/,
                      bool /*stream*/, int iterations)
{
    echoflow::Config cfg = loadConfig(configPath, modelDir);
    echoflow::CrispAsrEngine asr(cfg);

    for (int i = 0; i < iterations; ++i) {
        auto started = Clock::now();
        std::string text = asr.transcribe(audio);
        double transcribeMs = elapsedMs(started);
        if (cfg.stripTrailingPunctuation) {
            text = echoflow::stripPunctuation(text);
        }

        std::printf("{\"mode\":\"transcribe-file\",\"iteration\":%d,"
                    "\"transcribe_ms\":%.3f,\"chars\":%zu,"
                    "\"audio\":\"%s\",\"model\":\"%s\"}\n",
                    i + 1,
                    transcribeMs, text.size(), jsonEscape(audio.string()).c_str(),
                    jsonEscape(cfg.crispModelPath).c_str());
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    bool synthetic = false;
    std::optional<fs::path> transcribeFile;
    std::optional<fs::path> configPath;
    std::optional<fs::path> modelDir;
    int iterations = 1;
    int threads = 0;
    int openBlasThreads = echoflow::Config::defaultConfig().openBlasThreads;
    bool skipSilence = false;
    bool stream = false;
    bool preload = true;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--session-synthetic") {
            synthetic = true;
        } else if (arg == "--transcribe-file" && i + 1 < argc) {
            transcribeFile = fs::path(argv[++i]);
        } else if (arg == "--config" && i + 1 < argc) {
            configPath = fs::path(argv[++i]);
        } else if (arg == "--model-dir" && i + 1 < argc) {
            modelDir = fs::path(argv[++i]);
        } else if (arg == "--threads" && i + 1 < argc) {
            threads = std::max(1, std::stoi(argv[++i]));
        } else if (arg == "--openblas-threads" && i + 1 < argc) {
            openBlasThreads = std::max(1, std::stoi(argv[++i]));
        } else if (arg == "--skip-silence") {
            skipSilence = true;
        } else if (arg == "--stream") {
            stream = true;
        } else if (arg == "--iterations" && i + 1 < argc) {
            iterations = std::max(1, std::stoi(argv[++i]));
        } else if (arg == "--no-preload") {
            preload = false;
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
            printUsage();
            return 2;
        }
    }

    if (synthetic == transcribeFile.has_value()) {
        printUsage();
        return 2;
    }

    if (synthetic) {
        return runSynthetic(iterations);
    }
    return runTranscribeFile(*transcribeFile, configPath, modelDir, preload,
                             threads, openBlasThreads, skipSilence, stream, iterations);
}
