// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AsrEngine.h"

#include "ModelCatalog.h"
#include "SelfTest.h"
#include "log.h"

extern "C" {
#include <cblas.h>
#include "qwen_asr.h"
#include "qwen_asr_audio.h"
}

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <poll.h>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace echoflow {

namespace {

struct LiveTokenCallbackState {
    std::string text;
    std::function<void(const std::string&)> callback;
    std::chrono::steady_clock::time_point started;
    std::chrono::steady_clock::time_point lastTokenAt;
    std::chrono::steady_clock::time_point finalProgressAt;
    double finalProgressCursorMs = 0.0;
    double finalProgressTotalMs = 0.0;
    bool loggedFirstToken = false;
    bool loggedFinalProgress = false;
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

void liveStreamProgressCallback(double audioCursorMs,
                                double audioTotalMs,
                                int isFinal,
                                int emittedTokens,
                                void* userdata)
{
    if (!userdata) {
        return;
    }

    auto* state = static_cast<LiveTokenCallbackState*>(userdata);
    if (isFinal) {
        state->finalProgressAt = std::chrono::steady_clock::now();
        state->finalProgressCursorMs = audioCursorMs;
        state->finalProgressTotalMs = audioTotalMs;
        state->loggedFinalProgress = true;
        log("final live ASR chunk completed at cursor=" + std::to_string(audioCursorMs / 1000.0) +
            "s, total=" + std::to_string(audioTotalMs / 1000.0) +
            "s, emitted_tokens=" + std::to_string(emittedTokens));
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

class StreamProgressCallbackScope {
public:
    StreamProgressCallbackScope(qwen_ctx_t* ctx, qwen_stream_progress_cb callback, void* userdata)
        : ctx_(ctx)
    {
        qwen_set_stream_progress_callback(ctx_, callback, userdata);
    }

    ~StreamProgressCallbackScope()
    {
        qwen_set_stream_progress_callback(ctx_, nullptr, nullptr);
    }

    StreamProgressCallbackScope(const StreamProgressCallbackScope&) = delete;
    StreamProgressCallbackScope& operator=(const StreamProgressCallbackScope&) = delete;

private:
    qwen_ctx_t* ctx_ = nullptr;
};

std::filesystem::path senseVoiceModelPath(const Config& cfg)
{
    return resolveModelDir(cfg) / "sensevoice-small-q8.gguf";
}

std::filesystem::path senseVoiceVadPath(const Config& cfg)
{
    return resolveModelDir(cfg) / "fsmn-vad.gguf";
}

std::string trimText(const std::string& value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string executableDir()
{
    std::vector<char> buffer(4096);
    ssize_t n = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (n <= 0) {
        return {};
    }
    buffer[static_cast<size_t>(n)] = '\0';
    std::filesystem::path p(buffer.data());
    return p.parent_path().string();
}

std::string senseVoiceBinary()
{
    if (const char* override = std::getenv("ECHOFLOW_SENSEVOICE_BIN")) {
        if (*override) {
            return override;
        }
    }

    const std::string dir = executableDir();
    if (!dir.empty()) {
        const std::filesystem::path colocated =
            std::filesystem::path(dir) / "llama-funasr-sensevoice";
        if (access(colocated.c_str(), X_OK) == 0) {
            return colocated.string();
        }
    }
    return "llama-funasr-sensevoice";
}

void appendPipeData(int fd, std::string& out)
{
    std::array<char, 4096> buffer{};
    while (true) {
        ssize_t n = read(fd, buffer.data(), buffer.size());
        if (n > 0) {
            out.append(buffer.data(), static_cast<size_t>(n));
            continue;
        }
        if (n == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        break;
    }
}

void setNonBlocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

std::pair<std::string, std::string> readProcessPipes(int stdoutFd, int stderrFd)
{
    std::string stdoutText;
    std::string stderrText;
    bool stdoutOpen = true;
    bool stderrOpen = true;

    while (stdoutOpen || stderrOpen) {
        std::array<pollfd, 2> fds{{
            {stdoutOpen ? stdoutFd : -1, POLLIN | POLLHUP, 0},
            {stderrOpen ? stderrFd : -1, POLLIN | POLLHUP, 0},
        }};
        int ready = poll(fds.data(), fds.size(), -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if (stdoutOpen && (fds[0].revents & (POLLIN | POLLHUP | POLLERR))) {
            appendPipeData(stdoutFd, stdoutText);
            if (fds[0].revents & (POLLHUP | POLLERR)) {
                stdoutOpen = false;
            }
        }
        if (stderrOpen && (fds[1].revents & (POLLIN | POLLHUP | POLLERR))) {
            appendPipeData(stderrFd, stderrText);
            if (fds[1].revents & (POLLHUP | POLLERR)) {
                stderrOpen = false;
            }
        }
    }

    return {stdoutText, stderrText};
}

std::pair<int, std::string> runSenseVoiceProcess(const std::vector<std::string>& args)
{
    int stdoutPipe[2] = {-1, -1};
    int stderrPipe[2] = {-1, -1};
    if (pipe(stdoutPipe) != 0 || pipe(stderrPipe) != 0) {
        throw std::runtime_error(std::string("pipe failed: ") + std::strerror(errno));
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);
        close(stderrPipe[0]);
        close(stderrPipe[1]);
        throw std::runtime_error(std::string("fork failed: ") + std::strerror(errno));
    }

    if (pid == 0) {
        dup2(stdoutPipe[1], STDOUT_FILENO);
        dup2(stderrPipe[1], STDERR_FILENO);
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);
        close(stderrPipe[0]);
        close(stderrPipe[1]);

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }

    close(stdoutPipe[1]);
    close(stderrPipe[1]);
    setNonBlocking(stdoutPipe[0]);
    setNonBlocking(stderrPipe[0]);
    auto [stdoutText, stderrText] = readProcessPipes(stdoutPipe[0], stderrPipe[0]);
    close(stdoutPipe[0]);
    close(stderrPipe[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            break;
        }
    }

    if (!stderrText.empty()) {
        log("sensevoice stderr: " + trimText(stderrText));
    }
    if (WIFEXITED(status)) {
        return {WEXITSTATUS(status), stdoutText};
    }
    if (WIFSIGNALED(status)) {
        return {128 + WTERMSIG(status), stdoutText};
    }
    return {1, stdoutText};
}

}  // namespace

AsrEngine::AsrEngine(Config cfg)
    : cfg_(std::move(cfg))
{
}

AsrEngine::~AsrEngine()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (ctx_) {
        qwen_free(ctx_);
    }
}

bool AsrEngine::ensureLoadedLocked()
{
    if (isSenseVoiceModel(cfg_.modelName)) {
        const auto model = senseVoiceModelPath(cfg_);
        const auto vad = senseVoiceVadPath(cfg_);
        if (!std::filesystem::exists(model) || !std::filesystem::exists(vad)) {
            log("sensevoice model files missing under: " + resolveModelDir(cfg_).string());
            return false;
        }
        return true;
    }

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
    std::lock_guard<std::mutex> lock(mutex_);
    return ensureLoadedLocked();
}

std::string AsrEngine::transcribe(const std::filesystem::path& audio)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ensureLoadedLocked()) {
        return {};
    }

    auto started = std::chrono::steady_clock::now();
    if (isSenseVoiceModel(cfg_.modelName)) {
        const std::vector<std::string> args = {
            senseVoiceBinary(),
            "-m",
            senseVoiceModelPath(cfg_).string(),
            "--vad",
            senseVoiceVadPath(cfg_).string(),
            "-a",
            audio.string(),
        };
        log("transcribing audio with SenseVoice: " + audio.string());
        auto [exitCode, text] = runSenseVoiceProcess(args);
        if (exitCode != 0) {
            log("sensevoice runtime failed: exit=" + std::to_string(exitCode));
            return {};
        }
        text = trimText(text);
        auto elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        log("sensevoice transcription finished in " + std::to_string(elapsed) +
            "s, chars=" + std::to_string(text.size()));
        return text;
    }

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
    std::lock_guard<std::mutex> lock(mutex_);
    if (!liveAudio || !ensureLoadedLocked()) {
        return {};
    }

    auto* live = static_cast<qwen_live_audio_t*>(liveAudio);
    auto started = std::chrono::steady_clock::now();
    log("transcribing live audio stream");
    LiveTokenCallbackState callbackState;
    callbackState.started = started;
    callbackState.callback = std::move(partialTextCallback);
    std::unique_ptr<TokenCallbackScope> callbackScope;
    std::unique_ptr<StreamProgressCallbackScope> progressCallbackScope;
    if (callbackState.callback) {
        callbackScope =
            std::make_unique<TokenCallbackScope>(ctx_, liveTokenCallback, &callbackState);
        progressCallbackScope = std::make_unique<StreamProgressCallbackScope>(
            ctx_, liveStreamProgressCallback, &callbackState);
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
    if (callbackState.loggedFinalProgress) {
        const auto progressReturnGap =
            std::chrono::duration<double>(returnedAt - callbackState.finalProgressAt).count();
        log("final live ASR progress preceded return by " + std::to_string(progressReturnGap) +
            "s");
    }
    log("live transcription finished in " + std::to_string(elapsed) + "s, chars=" + std::to_string(text.size()));
    return text;
}

}  // namespace echoflow
