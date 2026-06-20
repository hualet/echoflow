// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PipeWireLiveVoicePipeline.h"

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
#include <unistd.h>
#include <utility>
#include <vector>

extern char** environ;

namespace echoflow {

namespace {

using Clock = std::chrono::steady_clock;
constexpr int kLiveSampleRate = 16000;
constexpr int kLiveChannels = 1;
constexpr const char* kLiveFormat = "s16";

double elapsedSeconds(Clock::time_point started)
{
    return std::chrono::duration<double>(Clock::now() - started).count();
}

void closeIfOpen(int& fd)
{
    if (fd != -1) {
        close(fd);
        fd = -1;
    }
}

bool waitForChild(pid_t child, int attempts)
{
    int status = 0;
    for (int i = 0; i < attempts; ++i) {
        pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child) {
            return true;
        }
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return true;
        }
        usleep(100000);
    }
    return false;
}

}  // namespace

PipeWireLiveVoicePipeline::PipeWireLiveVoicePipeline(Config cfg, AsrEngine& asr)
    : cfg_(std::move(cfg))
    , asr_(asr)
{
}

PipeWireLiveVoicePipeline::~PipeWireLiveVoicePipeline()
{
    cancel();
}

void PipeWireLiveVoicePipeline::start()
{
    if (active_) {
        return;
    }

    AudioSegmenterConfig segmenterConfig;
    segmenterConfig.sampleRate = kLiveSampleRate;
    auto nextSegmenter = std::make_unique<AudioSegmenter>(segmenterConfig);
    auto nextWorker = std::make_unique<SegmentAsrWorker>(
        asr_,
        runtimeDir() / "echoflow-segments",
        [this](int sequence, const std::string& text) {
            handleSegmentText(sequence, text);
        });

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
            throw std::runtime_error(std::string("dup pipe read fd failed: ") +
                                     std::strerror(errno));
        }
    }

    posix_spawn_file_actions_t actions;
    bool actionsInitialized = false;
    int initError = posix_spawn_file_actions_init(&actions);
    if (initError != 0) {
        close(readFd);
        close(pipeFds[1]);
        throw std::runtime_error(std::string("failed to initialize pw-record spawn actions: ") +
                                 std::strerror(initError));
    }
    actionsInitialized = true;

    auto cleanupActions = [&actions, &actionsInitialized]() {
        if (actionsInitialized) {
            posix_spawn_file_actions_destroy(&actions);
            actionsInitialized = false;
        }
    };

    int actionError = posix_spawn_file_actions_adddup2(&actions, pipeFds[1], STDOUT_FILENO);
    if (actionError == 0 && readFd != STDOUT_FILENO) {
        actionError = posix_spawn_file_actions_addclose(&actions, readFd);
    }
    if (actionError == 0 && pipeFds[1] != STDOUT_FILENO) {
        actionError = posix_spawn_file_actions_addclose(&actions, pipeFds[1]);
    }
    if (actionError != 0) {
        cleanupActions();
        close(readFd);
        close(pipeFds[1]);
        throw std::runtime_error(std::string("failed to configure pw-record spawn actions: ") +
                                 std::strerror(actionError));
    }

    std::vector<std::string> args = {
        "pw-record",
        "--rate", std::to_string(kLiveSampleRate),
        "--channels", std::to_string(kLiveChannels),
        "--format", kLiveFormat,
        "--raw",
        "-",
    };

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (auto& arg : args) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    pid_t pid = -1;
    int spawnError = posix_spawnp(&pid, "pw-record", &actions, nullptr, argv.data(), environ);
    cleanupActions();
    if (spawnError != 0) {
        close(readFd);
        close(pipeFds[1]);
        throw std::runtime_error(std::string("posix_spawnp pw-record failed: ") +
                                 std::strerror(spawnError));
    }

    close(pipeFds[1]);
    child_ = pid;
    readFd_ = readFd;
    segmenter_ = std::move(nextSegmenter);
    segmentWorker_ = std::move(nextWorker);
    cancelled_ = false;
    clearStableText();
    startedAt_ = Clock::now();
    active_ = true;

    try {
        segmentWorker_->start();
        readerThread_ = std::thread(&PipeWireLiveVoicePipeline::readerLoop, this);
    } catch (...) {
        cancel();
        throw;
    }

    log("live recording started");
}

std::string PipeWireLiveVoicePipeline::finish()
{
    if (!active_) {
        return {};
    }

    auto finishStarted = Clock::now();
    log("live recording stop requested after " + std::to_string(elapsedSeconds(startedAt_)) +
        "s");
    stopRecorder();
    cleanupProcess();
    closeReadFd();
    joinThreads();
    bool completed = true;
    if (segmentWorker_) {
        completed = segmentWorker_->finishAndWait(asrFinishTimeout());
    }
    active_ = false;
    segmenter_.reset();
    segmentWorker_.reset();
    std::string finalText = cancelled_ ? std::string() : stableText();
    clearStableText();
    log("live pipeline finish returned in " + std::to_string(elapsedSeconds(finishStarted)) +
        "s, total=" + std::to_string(elapsedSeconds(startedAt_)) +
        "s, chars=" + std::to_string(finalText.size()) +
        ", completed=" + (completed ? "yes" : "no"));
    return finalText;
}

void PipeWireLiveVoicePipeline::cancel()
{
    try {
        cancelled_ = true;
        stopRecorder();
        cleanupProcess();
        closeReadFd();
        joinThreads();
        if (segmentWorker_) {
            segmentWorker_->cancelAndWait();
        }
        active_ = false;
        segmenter_.reset();
        segmentWorker_.reset();
        clearStableText();
    } catch (const std::exception& e) {
        log(std::string("live pipeline cancel failed: ") + e.what());
    } catch (...) {
        log("live pipeline cancel failed");
    }
}

void PipeWireLiveVoicePipeline::setPartialTextCallback(
    std::function<void(const std::string&)> callback)
{
    std::lock_guard<std::mutex> lock(callbackMutex_);
    partialTextCallback_ = std::move(callback);
}

void PipeWireLiveVoicePipeline::readerLoop()
{
    try {
        int fd = readFd_;
        std::array<unsigned char, 64000> buffer;
        bool hasCarryByte = false;
        unsigned char carryByte = 0;

        while (fd != -1 && !cancelled_) {
            ssize_t n = read(fd, buffer.data(), buffer.size());
            if (n > 0) {
                const size_t byteCount = static_cast<size_t>(n);
                if (segmenter_) {
                    std::vector<int16_t> samples;
                    samples.reserve((byteCount + (hasCarryByte ? 1 : 0)) / sizeof(int16_t));

                    size_t offset = 0;
                    if (hasCarryByte && byteCount > 0) {
                        const unsigned int raw =
                            static_cast<unsigned int>(carryByte)
                            | (static_cast<unsigned int>(buffer[0]) << 8);
                        samples.push_back(static_cast<int16_t>(
                            raw >= 0x8000U ? static_cast<int>(raw) - 0x10000 : static_cast<int>(raw)));
                        hasCarryByte = false;
                        offset = 1;
                    }

                    for (; offset + 1 < byteCount; offset += sizeof(int16_t)) {
                        const unsigned int raw =
                            static_cast<unsigned int>(buffer[offset])
                            | (static_cast<unsigned int>(buffer[offset + 1]) << 8);
                        samples.push_back(static_cast<int16_t>(
                            raw >= 0x8000U ? static_cast<int>(raw) - 0x10000 : static_cast<int>(raw)));
                    }

                    if (offset < byteCount) {
                        carryByte = buffer[offset];
                        hasCarryByte = true;
                    }

                    if (!samples.empty()) {
                        enqueueSegments(segmenter_->append(samples.data(), samples.size()));
                    }
                }
                continue;
            }
            if (n == 0) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            log(std::string("live recording read failed: ") + std::strerror(errno));
            break;
        }
        if (hasCarryByte) {
            log("live recording dropped incomplete trailing PCM byte");
        }
    } catch (const std::exception& e) {
        log(std::string("live recording reader failed: ") + e.what());
    } catch (...) {
        log("live recording reader failed");
    }

    try {
        if (!cancelled_) {
            flushSegmenter();
        }
    } catch (const std::exception& e) {
        log(std::string("live recording segment flush failed: ") + e.what());
    } catch (...) {
        log("live recording segment flush failed");
    }
}

void PipeWireLiveVoicePipeline::enqueueSegments(std::vector<AudioSegment> segments)
{
    if (!segmentWorker_) {
        return;
    }

    for (auto& segment : segments) {
        segmentWorker_->enqueue(std::move(segment));
    }
}

void PipeWireLiveVoicePipeline::flushSegmenter()
{
    if (!segmenter_ || !segmentWorker_) {
        return;
    }

    std::optional<AudioSegment> segment = segmenter_->flush();
    if (segment.has_value()) {
        segmentWorker_->enqueue(std::move(*segment));
    }
}

void PipeWireLiveVoicePipeline::handleSegmentText(int sequence, const std::string& text)
{
    if (cancelled_) {
        return;
    }

    std::string accumulated;
    {
        std::lock_guard<std::mutex> lock(textMutex_);
        textAccumulator_.append(sequence, text);
        stableText_ = textAccumulator_.text();
        accumulated = stableText_;
    }

    std::function<void(const std::string&)> callback;
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        callback = partialTextCallback_;
    }
    if (callback) {
        callback(accumulated);
    }
}

std::string PipeWireLiveVoicePipeline::stableText() const
{
    std::lock_guard<std::mutex> lock(textMutex_);
    return stableText_;
}

void PipeWireLiveVoicePipeline::clearStableText()
{
    std::lock_guard<std::mutex> lock(textMutex_);
    textAccumulator_.clear();
    stableText_.clear();
}

std::chrono::steady_clock::duration PipeWireLiveVoicePipeline::asrFinishTimeout() const
{
    const int timeoutSeconds = cfg_.asrTimeoutSeconds > 0 ? cfg_.asrTimeoutSeconds : 1;
    return std::chrono::seconds(timeoutSeconds);
}

void PipeWireLiveVoicePipeline::stopRecorder()
{
    if (child_ != -1) {
        kill(child_, SIGINT);
    }
}

void PipeWireLiveVoicePipeline::cleanupProcess()
{
    if (child_ == -1) {
        return;
    }

    pid_t child = child_;
    bool exited = waitForChild(child, 50);
    if (!exited) {
        kill(child, SIGTERM);
        exited = waitForChild(child, 20);
    }
    if (!exited) {
        kill(child, SIGKILL);
        exited = waitForChild(child, 20);
    }
    if (!exited) {
        log("live recorder did not exit after SIGKILL");
    }
    child_ = -1;
}

void PipeWireLiveVoicePipeline::closeReadFd()
{
    closeIfOpen(readFd_);
}

void PipeWireLiveVoicePipeline::joinThreads()
{
    if (readerThread_.joinable()) {
        readerThread_.join();
    }
}

}  // namespace echoflow
