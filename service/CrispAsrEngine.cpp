// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CrispAsrEngine.h"

#include "log.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace echoflow {

namespace {

// Map EchoFlow language names to CrispASR ISO codes. Returns empty for
// unknown/empty, in which case -l is omitted (CrispASR's LID then runs; it
// fails fast without the ggml-tiny.bin download when built without libcurl).
std::string languageCode(const std::string& value)
{
    if (value == "Chinese" || value == "chinese" || value == "zh" || value == "cmn") {
        return "zh";
    }
    if (value == "English" || value == "english" || value == "en") {
        return "en";
    }
    if (value.empty()) {
        return {};
    }
    return value;  // assume already an ISO code (ja, ko, ...)
}

}  // namespace

std::vector<std::string> CrispAsrEngine::buildArgs(const Config& cfg,
                                                   const std::filesystem::path& audio)
{
    std::vector<std::string> args = {
        cfg.crispBinary,
        "-m", cfg.crispModelPath,
        "--backend", cfg.crispBackend,
        "-f", audio.string(),
        "-t", std::to_string(cfg.crispThreads),
    };
    auto lang = languageCode(cfg.language.value_or(""));
    if (!lang.empty()) {
        args.push_back("-l");
        args.push_back(lang);
    }
    if (!cfg.crispExtraArgs.empty()) {
        args.push_back(cfg.crispExtraArgs);
    }
    return args;
}

CrispAsrEngine::CrispAsrEngine(Config cfg)
    : cfg_(std::move(cfg))
{
}

std::string CrispAsrEngine::transcribe(const std::filesystem::path& audio)
{
    auto args = buildArgs(cfg_, audio);

    int outPipe[2] = {-1, -1};
    if (pipe(outPipe) != 0) {
        log(std::string("crispasr pipe failed: ") + std::strerror(errno));
        return {};
    }

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(outPipe[0]);
        close(outPipe[1]);
        return {};
    }
    posix_spawn_file_actions_adddup2(&actions, outPipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, outPipe[0]);
    posix_spawn_file_actions_addclose(&actions, outPipe[1]);

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (auto& a : args) {
        argv.push_back(a.data());
    }
    argv.push_back(nullptr);

    pid_t pid = -1;
    int rc = posix_spawnp(&pid, cfg_.crispBinary.c_str(), &actions, nullptr,
                          argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(outPipe[1]);
    if (rc != 0) {
        log(std::string("posix_spawnp crispasr failed: ") + std::strerror(rc));
        close(outPipe[0]);
        return {};
    }

    std::string output;
    std::array<char, 4096> buf{};
    ssize_t n = 0;
    while ((n = read(outPipe[0], buf.data(), buf.size())) > 0) {
        output.append(buf.data(), static_cast<size_t>(n));
    }
    close(outPipe[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {
        // retry on EINTR
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        log("crispasr exited non-zero; partial output: " + output);
    }

    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }
    return output;
}

}  // namespace echoflow
