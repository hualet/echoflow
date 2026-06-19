// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PipeWireLiveVoicePipeline.h"

#include "log.h"

#include <array>
#include <cerrno>
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

    auto nextLive = std::make_unique<LiveAudioBuffer>();

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
        "--rate", "16000",
        "--channels", "1",
        "--format", "s16",
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
    live_ = std::move(nextLive);
    cancelled_ = false;
    result_.clear();
    active_ = true;

    try {
        readerThread_ = std::thread(&PipeWireLiveVoicePipeline::readerLoop, this);
        asrThread_ = std::thread(&PipeWireLiveVoicePipeline::asrLoop, this);
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

    stopRecorder();
    cleanupProcess();
    if (live_) {
        live_->markEof();
    }
    joinThreads();
    closeReadFd();
    active_ = false;
    live_.reset();
    return cancelled_ ? std::string() : result_;
}

void PipeWireLiveVoicePipeline::cancel()
{
    try {
        cancelled_ = true;
        stopRecorder();
        cleanupProcess();
        if (live_) {
            live_->markEof();
        }
        joinThreads();
        closeReadFd();
        active_ = false;
        live_.reset();
        result_.clear();
    } catch (const std::exception& e) {
        log(std::string("live pipeline cancel failed: ") + e.what());
    } catch (...) {
        log("live pipeline cancel failed");
    }
}

void PipeWireLiveVoicePipeline::readerLoop()
{
    try {
        int fd = readFd_;
        std::array<unsigned char, 64000> buffer;

        while (fd != -1) {
            ssize_t n = read(fd, buffer.data(), buffer.size());
            if (n > 0) {
                if (live_) {
                    live_->appendS16le(buffer.data(), static_cast<size_t>(n));
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
    } catch (const std::exception& e) {
        log(std::string("live recording reader failed: ") + e.what());
    } catch (...) {
        log("live recording reader failed");
    }

    try {
        if (live_) {
            live_->markEof();
        }
    } catch (const std::exception& e) {
        log(std::string("live recording EOF failed: ") + e.what());
    } catch (...) {
        log("live recording EOF failed");
    }
}

void PipeWireLiveVoicePipeline::asrLoop()
{
    try {
        result_ = live_ ? asr_.transcribeLive(live_->get()) : std::string();
    } catch (const std::exception& e) {
        log(std::string("live ASR failed: ") + e.what());
        result_.clear();
    } catch (...) {
        log("live ASR failed");
        result_.clear();
    }
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
    if (asrThread_.joinable()) {
        asrThread_.join();
    }
}

}  // namespace echoflow
