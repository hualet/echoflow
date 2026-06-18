// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Recorder.h"

#include "log.h"

#include <csignal>
#include <ctime>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace echoflow {
namespace fs = std::filesystem;

namespace {

std::string timestamp()
{
    std::time_t now = std::time(nullptr);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", std::localtime(&now));
    return buffer;
}

}  // namespace

PipeWireRecorder::PipeWireRecorder(Config cfg)
    : cfg_(std::move(cfg))
{
}

void PipeWireRecorder::start()
{
    if (child_ != -1) {
        return;
    }

    fs::create_directories(cfg_.recordingsDir);
    path_ = fs::path(cfg_.recordingsDir) / ("voice-" + timestamp() + ".wav");

    std::string rate = std::to_string(cfg_.pwRecord.rate);
    std::string channels = std::to_string(cfg_.pwRecord.channels);
    std::vector<std::string> args = {
        "pw-record",
        "--rate", rate,
        "--channels", channels,
        "--format", cfg_.pwRecord.format,
        path_.string(),
    };

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (auto& arg : args) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    pid_t pid = -1;
    if (posix_spawnp(&pid, "pw-record", nullptr, nullptr, argv.data(), environ) != 0) {
        log("posix_spawnp pw-record failed");
        child_ = -1;
        path_.clear();
        return;
    }

    child_ = pid;
    startedAt_ = std::chrono::steady_clock::now();
    log("recording started: " + path_.string());
}

fs::path PipeWireRecorder::stop()
{
    if (child_ == -1) {
        return {};
    }

    double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt_).count();
    pid_t pid = child_;
    fs::path path = path_;
    child_ = -1;
    path_.clear();

    kill(pid, SIGINT);
    int status = 0;
    bool exited = false;
    for (int i = 0; i < 50; ++i) {
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            exited = true;
            break;
        }
        if (result < 0) {
            exited = true;
            break;
        }
        usleep(100000);
    }
    if (!exited) {
        kill(pid, SIGTERM);
        waitpid(pid, &status, 0);
    }

    if (elapsed < cfg_.minRecordSeconds) {
        log("recording too short");
        return {};
    }

    std::error_code ec;
    auto size = fs::file_size(path, ec);
    if (ec || size < 1024) {
        log("recording missing or too small");
        return {};
    }

    return path;
}

}  // namespace echoflow
