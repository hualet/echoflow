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
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace echoflow {

using Clock = std::chrono::steady_clock;

namespace {

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

std::vector<float> toFloat32(const std::vector<int16_t>& s16)
{
    std::vector<float> out;
    out.reserve(s16.size());
    for (auto s : s16) out.push_back(static_cast<float>(s) / 32768.0f);
    return out;
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
    if (!lang.empty()) session_->setLanguage(lang);

    AudioSegmenterConfig segCfg;
    segCfg.sampleRate = 16000;
    segmenter_ = std::make_unique<AudioSegmenter>(segCfg);

    int pipeFds[2] = {-1, -1};
    if (pipe(pipeFds) != 0)
        throw std::runtime_error(std::string("pipe failed: ") + std::strerror(errno));
    int readFd = pipeFds[0];
    if (readFd == STDOUT_FILENO) {
        readFd = dup(pipeFds[0]); close(pipeFds[0]); pipeFds[0] = readFd;
    }

    posix_spawn_file_actions_t a;
    if (posix_spawn_file_actions_init(&a) != 0) {
        close(readFd); close(pipeFds[1]);
        throw std::runtime_error("pw-record spawn init failed");
    }
    posix_spawn_file_actions_adddup2(&a, pipeFds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&a, readFd);
    posix_spawn_file_actions_addclose(&a, pipeFds[1]);

    auto args = buildPipeWireLiveRecordArgs(cfg_);
    std::vector<char*> argv; argv.reserve(args.size() + 1);
    for (auto& s : args) argv.push_back(s.data());
    argv.push_back(nullptr);

    pid_t pid = -1;
    int rc = posix_spawnp(&pid, "pw-record", &a, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&a);
    if (rc != 0) {
        close(readFd); close(pipeFds[1]);
        throw std::runtime_error(std::string("posix_spawnp pw-record failed: ") + std::strerror(rc));
    }
    close(pipeFds[1]);

    recorderChild_ = pid;
    readFd_ = readFd;
    cancelled_ = false;
    {
        std::lock_guard<std::mutex> lock(segmentMutex_);
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
                std::vector<int16_t> samples;
                samples.reserve((byteCount + (hasCarry ? 1 : 0)) / 2);
                size_t offset = 0;
                if (hasCarry && byteCount > 0) {
                    unsigned int raw = static_cast<unsigned int>(carry)
                        | (static_cast<unsigned int>(buf[0]) << 8);
                    samples.push_back(raw >= 0x8000U ? static_cast<int>(raw) - 0x10000
                                                     : static_cast<int>(raw));
                    hasCarry = false; offset = 1;
                }
                for (; offset + 1 < byteCount; offset += 2) {
                    unsigned int raw = static_cast<unsigned int>(buf[offset])
                        | (static_cast<unsigned int>(buf[offset + 1]) << 8);
                    samples.push_back(raw >= 0x8000U ? static_cast<int>(raw) - 0x10000
                                                     : static_cast<int>(raw));
                }
                if (offset < byteCount) { carry = buf[offset]; hasCarry = true; }

                // Feed through energy-VAD segmenter
                auto segments = segmenter_->append(samples.data(), samples.size());

                // Transcribe completed segments with warm model
                if (!segments.empty() && session_) {
                    for (auto& seg : segments) {
                        auto f32 = toFloat32(seg.samples);
                        std::string text = session_->transcribe(
                            f32.data(), static_cast<int>(f32.size()));
                        {
                            std::lock_guard<std::mutex> lock(segmentMutex_);
                            if (!text.empty()) results_.push_back(text);
                        }
                    }
                    // Emit accumulated text as partial
                    std::string joined;
                    {
                        std::lock_guard<std::mutex> lock(segmentMutex_);
                        for (const auto& r : results_) {
                            if (!r.empty()) {
                                if (!joined.empty() && joined.back() != ' ' && r.front() != ' ')
                                    joined += " ";
                                joined += r;
                            }
                        }
                    }
                    emitText(joined);
                }
                continue;
            }
            if (n == 0) break;
            if (errno == EINTR) continue;
            break;
        }
        // Flush any remaining open segment
        if (!cancelled_ && segmenter_) {
            auto remaining = segmenter_->flush();
            if (remaining.has_value() && session_) {
                auto f32 = toFloat32(remaining->samples);
                std::string text = session_->transcribe(
                    f32.data(), static_cast<int>(f32.size()));
                if (!text.empty()) {
                    std::lock_guard<std::mutex> lock(segmentMutex_);
                    results_.push_back(text);
                }
            }
        }
    } catch (const std::exception& e) {
        log(std::string("crisp live reader failed: ") + e.what());
    } catch (...) {
        log("crisp live reader failed");
    }
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
    if (!cancelled_) {
        std::lock_guard<std::mutex> lock(segmentMutex_);
        for (const auto& r : results_) {
            if (!r.empty()) {
                if (!finalText.empty() && finalText.back() != ' ' && r.front() != ' ')
                    finalText += " ";
                finalText += r;
            }
        }
    }
    active_ = false;
    segmenter_.reset();
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
        segmenter_.reset();
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
