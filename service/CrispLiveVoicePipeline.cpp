// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CrispLiveVoicePipeline.h"

#include "CrispAsrEngine.h"
#include "CrispSession.h"
#include "Recorder.h"
#include "log.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace echoflow {

using Clock = std::chrono::steady_clock;

namespace {

constexpr int kSampleRate = 16000;
constexpr int kChunkMs = 5000;

double elapsedSeconds(Clock::time_point started)
{
    return std::chrono::duration<double>(Clock::now() - started).count();
}

bool waitForChild(pid_t child, int attempts)
{
    int status = 0;
    for (int i = 0; i < attempts; ++i) {
        pid_t r = waitpid(child, &status, WNOHANG);
        if (r == child) return true;
        if (r < 0 && errno != EINTR) return true;
        usleep(100000);
    }
    return false;
}

}  // namespace

CrispLiveVoicePipeline::CrispLiveVoicePipeline(Config cfg)
    : cfg_(std::move(cfg))
{
}

CrispLiveVoicePipeline::~CrispLiveVoicePipeline()
{
    cancel();
}

void CrispLiveVoicePipeline::setPartialTextCallback(
    std::function<void(const std::string&)> callback)
{
    std::lock_guard<std::mutex> lock(callbackMutex_);
    partialTextCallback_ = std::move(callback);
}

void CrispLiveVoicePipeline::start()
{
    if (active_) return;

    session_ = std::make_unique<CrispSession>(cfg_.crispModelPath, cfg_.crispBackend,
                                              cfg_.crispThreads);
    if (!session_->isLoaded()) {
        session_.reset();
        throw std::runtime_error("failed to load crisp model: " + cfg_.crispModelPath);
    }
    auto lang = CrispAsrEngine::languageCode(cfg_.language.value_or(""));
    if (!lang.empty()) {
        session_->setLanguage(lang);
    }

    int pipeFds[2] = {-1, -1};
    if (pipe(pipeFds) != 0) {
        throw std::runtime_error(std::string("pipe failed: ") + std::strerror(errno));
    }
    int readFd = pipeFds[0];
    if (readFd == STDOUT_FILENO) {
        readFd = dup(pipeFds[0]);
        close(pipeFds[0]);
        pipeFds[0] = readFd;
        if (readFd < 0) {
            close(pipeFds[1]);
            throw std::runtime_error(std::string("dup pipe failed: ") + std::strerror(errno));
        }
    }

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(readFd); close(pipeFds[1]);
        throw std::runtime_error("pw-record spawn actions init failed");
    }
    posix_spawn_file_actions_adddup2(&actions, pipeFds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, readFd);
    posix_spawn_file_actions_addclose(&actions, pipeFds[1]);

    auto recArgs = buildPipeWireLiveRecordArgs(cfg_);
    std::vector<char*> recArgv;
    recArgv.reserve(recArgs.size() + 1);
    for (auto& a : recArgs) recArgv.push_back(a.data());
    recArgv.push_back(nullptr);

    pid_t pid = -1;
    int rc = posix_spawnp(&pid, "pw-record", &actions, nullptr, recArgv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0) {
        close(readFd); close(pipeFds[1]);
        throw std::runtime_error(std::string("posix_spawnp pw-record failed: ") + std::strerror(rc));
    }
    close(pipeFds[1]);

    recorderChild_ = pid;
    readFd_ = readFd;
    cancelled_ = false;
    {
        std::lock_guard<std::mutex> lock(pcmMutex_);
        pcmBuffer_.clear();
        pcmChunk_.clear();
        results_.clear();
    }
    startedAt_ = Clock::now();
    active_ = true;

    readerThread_ = std::thread(&CrispLiveVoicePipeline::readerLoop, this);
    log("crisp live pipeline started: source=" +
        (cfg_.pwRecord.source.empty() ? std::string("default") : cfg_.pwRecord.source));
}

void CrispLiveVoicePipeline::readerLoop()
{
    try {
        std::array<unsigned char, 64000> buf{};
        bool hasCarry = false;
        unsigned char carry = 0;
        while (readFd_ != -1 && !cancelled_) {
            ssize_t n = read(readFd_, buf.data(), buf.size());
            if (n > 0) {
                size_t byteCount = static_cast<size_t>(n);
                std::vector<float> chunk;
                {
                    std::lock_guard<std::mutex> lock(pcmMutex_);
                    size_t offset = 0;
                    if (hasCarry && byteCount > 0) {
                        unsigned int raw = static_cast<unsigned int>(carry)
                            | (static_cast<unsigned int>(buf[0]) << 8);
                        int16_t s16 = raw >= 0x8000U ? static_cast<int>(raw) - 0x10000
                                                     : static_cast<int>(raw);
                        pcmBuffer_.push_back(static_cast<float>(s16) / 32768.0f);
                        pcmChunk_.push_back(static_cast<float>(s16) / 32768.0f);
                        hasCarry = false;
                        offset = 1;
                    }
                    for (; offset + 1 < byteCount; offset += 2) {
                        unsigned int raw = static_cast<unsigned int>(buf[offset])
                            | (static_cast<unsigned int>(buf[offset + 1]) << 8);
                        int16_t s16 = raw >= 0x8000U ? static_cast<int>(raw) - 0x10000
                                                     : static_cast<int>(raw);
                        float f = static_cast<float>(s16) / 32768.0f;
                        pcmBuffer_.push_back(f);
                        pcmChunk_.push_back(f);
                    }
                    if (offset < byteCount) {
                        carry = buf[offset];
                        hasCarry = true;
                    }
                }

                // Trigger chunk transcription if accumulated enough
                {
                    std::lock_guard<std::mutex> lock(pcmMutex_);
                    int chunkSamples = kChunkMs * kSampleRate / 1000;
                    if (static_cast<int>(pcmChunk_.size()) >= chunkSamples) {
                        chunk = std::move(pcmChunk_);
                        pcmChunk_.clear();
                    }
                }
                if (!chunk.empty() && session_) {
                    std::string text = session_->transcribe(
                        chunk.data(), static_cast<int>(chunk.size()));
                    {
                        std::lock_guard<std::mutex> lock(pcmMutex_);
                        results_.push_back(text);
                    }
                    emitChunkedText();
                }
                continue;
            }
            if (n == 0) break;
            if (errno == EINTR) continue;
            log(std::string("live recording read failed: ") + std::strerror(errno));
            break;
        }
    } catch (const std::exception& e) {
        log(std::string("crisp live reader failed: ") + e.what());
    } catch (...) {
        log("crisp live reader failed");
    }
}

void CrispLiveVoicePipeline::emitChunkedText()
{
    std::string stable;
    {
        std::lock_guard<std::mutex> lock(pcmMutex_);
        for (const auto& r : results_) {
            if (!r.empty()) {
                if (!stable.empty() && stable.back() != ' ' && !r.empty() && r.front() != ' ') {
                    stable += " ";
                }
                stable += r;
            }
        }
        // Last chunk in-progress is not transcribed yet — show evolving partial
        if (!pcmChunk_.empty() && session_) {
            auto partial = pcmChunk_;
            std::string partText = session_->transcribe(
                partial.data(), static_cast<int>(partial.size()));
            if (!partText.empty()) {
                if (!stable.empty() && stable.back() != ' ' && partText.front() != ' ') {
                    stable += " ";
                }
                stable += partText;
            }
        }
    }
    emitText(stable);
}

std::string CrispLiveVoicePipeline::finish()
{
    if (!active_) return {};
    auto finishStarted = Clock::now();
    log("crisp live pipeline stop after " + std::to_string(elapsedSeconds(startedAt_)) + "s");

    stopRecorder();
    reapChild(recorderChild_);
    close(readFd_); readFd_ = -1;
    if (readerThread_.joinable()) readerThread_.join();

    std::string finalText;
    if (!cancelled_ && session_) {
        // Transcribe only the last open chunk (small), then join all results
        std::string lastPart;
        {
            std::lock_guard<std::mutex> lock(pcmMutex_);
            if (!pcmChunk_.empty()) {
                auto pcm = pcmChunk_;
                lastPart = session_->transcribe(pcm.data(), static_cast<int>(pcm.size()));
                if (!lastPart.empty()) {
                    results_.push_back(lastPart);
                }
            }
            // Join all results
            for (const auto& r : results_) {
                if (!r.empty()) {
                    if (!finalText.empty() && finalText.back() != ' ' &&
                        r.front() != ' ') {
                        finalText += " ";
                    }
                    finalText += r;
                }
            }
        }
        // Fallback: if no chunks were transcribed (very short recording), do a single pass
        if (finalText.empty() && !pcmBuffer_.empty()) {
            std::lock_guard<std::mutex> lock(pcmMutex_);
            auto full = pcmBuffer_;
            finalText = session_->transcribe(full.data(), static_cast<int>(full.size()));
        }
    }
    active_ = false;
    session_.reset();
    log("crisp live finish in " + std::to_string(elapsedSeconds(finishStarted)) +
        "s, chars=" + std::to_string(finalText.size()));
    return finalText;
}

void CrispLiveVoicePipeline::cancel()
{
    try {
        cancelled_ = true;
        stopRecorder();
        reapChild(recorderChild_);
        if (readFd_ != -1) { close(readFd_); readFd_ = -1; }
        if (readerThread_.joinable()) readerThread_.join();
        active_ = false;
        session_.reset();
    } catch (const std::exception& e) {
        log(std::string("crisp pipeline cancel failed: ") + e.what());
    } catch (...) {
        log("crisp pipeline cancel failed");
    }
}

void CrispLiveVoicePipeline::stopRecorder()
{
    if (recorderChild_ != -1) kill(recorderChild_, SIGINT);
}

void CrispLiveVoicePipeline::reapChild(pid_t& child)
{
    if (child == -1) return;
    pid_t c = child;
    if (!waitForChild(c, 50)) {
        kill(c, SIGTERM);
        if (!waitForChild(c, 20)) {
            kill(c, SIGKILL);
            waitForChild(c, 20);
        }
    }
    child = -1;
}

void CrispLiveVoicePipeline::emitText(const std::string& text)
{
    std::function<void(const std::string&)> cb;
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        cb = partialTextCallback_;
    }
    if (cb) cb(text);
}

}  // namespace echoflow
