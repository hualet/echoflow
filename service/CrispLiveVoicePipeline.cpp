// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CrispLiveVoicePipeline.h"

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
#include <unistd.h>
#include <vector>

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
        if (r == child) {
            return true;
        }
        if (r < 0 && errno != EINTR) {
            return true;
        }
        usleep(100000);
    }
    return false;
}

std::string languageCode(const std::string& value)
{
    if (value == "Chinese" || value == "chinese" || value == "zh") {
        return "zh";
    }
    if (value == "English" || value == "english" || value == "en") {
        return "en";
    }
    return value;
}

}  // namespace

std::vector<std::string> CrispLiveVoicePipeline::buildCrispArgs(const Config& cfg)
{
    std::vector<std::string> args = {
        cfg.crispBinary,
        "--stream",
        "--stream-json",
        "-m", cfg.crispModelPath,
        "--backend", cfg.crispBackend,
        "--stream-final-mode", "redecode",
        "--stream-final-on-silence-ms", std::to_string(cfg.crispFinalOnSilenceMs),
        "-t", std::to_string(cfg.crispThreads),
    };
    if (cfg.crispVad) {
        args.push_back("--vad");
    }
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
    if (active_) {
        return;
    }

    int pwPipe[2] = {-1, -1};   // pw-record stdout -> crispasr stdin
    int outPipe[2] = {-1, -1};  // crispasr stdout -> parent parser
    if (pipe(pwPipe) != 0 || pipe(outPipe) != 0) {
        throw std::runtime_error(std::string("pipe failed: ") + std::strerror(errno));
    }

    auto closeBoth = [](int p[2]) {
        if (p[0] != -1) close(p[0]);
        if (p[1] != -1) close(p[1]);
    };

    // --- spawn pw-record (stdout -> pwPipe[1]) ---
    posix_spawn_file_actions_t ra;
    if (posix_spawn_file_actions_init(&ra) != 0) {
        closeBoth(pwPipe);
        closeBoth(outPipe);
        throw std::runtime_error("pw-record spawn actions init failed");
    }
    posix_spawn_file_actions_adddup2(&ra, pwPipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&ra, pwPipe[0]);
    posix_spawn_file_actions_addclose(&ra, outPipe[0]);
    posix_spawn_file_actions_addclose(&ra, outPipe[1]);

    auto recArgs = buildPipeWireLiveRecordArgs(cfg_);
    std::vector<char*> recArgv;
    recArgv.reserve(recArgs.size() + 1);
    for (auto& a : recArgs) {
        recArgv.push_back(a.data());
    }
    recArgv.push_back(nullptr);

    pid_t recPid = -1;
    int rc = posix_spawnp(&recPid, "pw-record", &ra, nullptr, recArgv.data(), environ);
    posix_spawn_file_actions_destroy(&ra);
    if (rc != 0) {
        closeBoth(pwPipe);
        closeBoth(outPipe);
        throw std::runtime_error(std::string("posix_spawnp pw-record failed: ") + std::strerror(rc));
    }
    close(pwPipe[1]);  // parent does not write to crispasr stdin

    // --- spawn crispasr (stdin <- pwPipe[0], stdout -> outPipe[1]) ---
    posix_spawn_file_actions_t ca;
    if (posix_spawn_file_actions_init(&ca) != 0) {
        kill(recPid, SIGTERM);
        waitForChild(recPid, 20);
        close(pwPipe[0]);
        closeBoth(outPipe);
        throw std::runtime_error("crispasr spawn actions init failed");
    }
    posix_spawn_file_actions_adddup2(&ca, pwPipe[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&ca, outPipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&ca, outPipe[0]);

    auto crispArgs = buildCrispArgs(cfg_);
    std::vector<char*> crispArgv;
    crispArgv.reserve(crispArgs.size() + 1);
    for (auto& a : crispArgs) {
        crispArgv.push_back(a.data());
    }
    crispArgv.push_back(nullptr);

    pid_t crispPid = -1;
    rc = posix_spawnp(&crispPid, cfg_.crispBinary.c_str(), &ca, nullptr,
                      crispArgv.data(), environ);
    posix_spawn_file_actions_destroy(&ca);
    close(pwPipe[0]);
    close(outPipe[1]);
    if (rc != 0) {
        kill(recPid, SIGTERM);
        waitForChild(recPid, 20);
        close(outPipe[0]);
        throw std::runtime_error(std::string("posix_spawnp crispasr failed: ") + std::strerror(rc));
    }

    recorderChild_ = recPid;
    crispChild_ = crispPid;
    crispOutFd_ = outPipe[0];
    accumulator_.clear();
    cancelled_ = false;
    startedAt_ = Clock::now();
    active_ = true;

    parserThread_ = std::thread(&CrispLiveVoicePipeline::parserLoop, this);
    log("crisp live pipeline started: source=" +
        (cfg_.pwRecord.source.empty() ? std::string("default") : cfg_.pwRecord.source));
}

void CrispLiveVoicePipeline::parserLoop()
{
    try {
        std::array<char, 8192> buf{};
        std::string pending;
        int fd = crispOutFd_;
        while (fd != -1 && !cancelled_) {
            ssize_t n = read(fd, buf.data(), buf.size());
            if (n > 0) {
                pending.append(buf.data(), static_cast<size_t>(n));
                size_t pos = 0;
                while (true) {
                    auto nl = pending.find('\n', pos);
                    if (nl == std::string::npos) {
                        break;
                    }
                    std::string line = pending.substr(pos, nl - pos);
                    pos = nl + 1;
                    std::optional<std::string> emitted;
                    {
                        std::lock_guard<std::mutex> lock(accumulatorMutex_);
                        emitted = accumulator_.processEvent(line);
                    }
                    if (emitted) {
                        emitText(*emitted);
                    }
                }
                pending.erase(0, pos);
                continue;
            }
            if (n == 0) {
                break;  // EOF: crispasr exited
            }
            if (errno == EINTR) {
                continue;
            }
            log(std::string("crispasr stdout read failed: ") + std::strerror(errno));
            break;
        }
    } catch (const std::exception& e) {
        log(std::string("crisp parser thread failed: ") + e.what());
    } catch (...) {
        log("crisp parser thread failed");
    }
}

void CrispLiveVoicePipeline::emitText(const std::string& text)
{
    std::function<void(const std::string&)> cb;
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        cb = partialTextCallback_;
    }
    if (cb) {
        cb(text);
    }
}

std::string CrispLiveVoicePipeline::finish()
{
    if (!active_) {
        return {};
    }
    auto finishStarted = Clock::now();
    log("crisp live pipeline stop requested after " + std::to_string(elapsedSeconds(startedAt_)) +
        "s");

    // SIGINT pw-record -> its stdout closes -> crispasr sees stdin EOF ->
    // flushes trailing finals and exits -> parser hits stdout EOF.
    stopRecorder();
    reapChild(recorderChild_);
    if (parserThread_.joinable()) {
        parserThread_.join();
    }
    reapChild(crispChild_);
    if (crispOutFd_ != -1) {
        close(crispOutFd_);
        crispOutFd_ = -1;
    }

    active_ = false;
    std::string finalText;
    if (!cancelled_) {
        std::lock_guard<std::mutex> lock(accumulatorMutex_);
        finalText = accumulator_.finalText();
    }
    log("crisp live pipeline finish returned in " + std::to_string(elapsedSeconds(finishStarted)) +
        "s, chars=" + std::to_string(finalText.size()));
    return finalText;
}

void CrispLiveVoicePipeline::cancel()
{
    try {
        cancelled_ = true;
        stopRecorder();
        reapChild(recorderChild_);
        if (crispChild_ != -1) {
            kill(crispChild_, SIGTERM);
        }
        if (parserThread_.joinable()) {
            parserThread_.join();
        }
        reapChild(crispChild_);
        if (crispOutFd_ != -1) {
            close(crispOutFd_);
            crispOutFd_ = -1;
        }
        active_ = false;
        std::lock_guard<std::mutex> lock(accumulatorMutex_);
        accumulator_.clear();
    } catch (const std::exception& e) {
        log(std::string("crisp live pipeline cancel failed: ") + e.what());
    } catch (...) {
        log("crisp live pipeline cancel failed");
    }
}

void CrispLiveVoicePipeline::stopRecorder()
{
    if (recorderChild_ != -1) {
        kill(recorderChild_, SIGINT);
    }
}

void CrispLiveVoicePipeline::reapChild(pid_t& child)
{
    if (child == -1) {
        return;
    }
    pid_t c = child;
    bool exited = waitForChild(c, 50);
    if (!exited) {
        kill(c, SIGTERM);
        exited = waitForChild(c, 20);
    }
    if (!exited) {
        kill(c, SIGKILL);
        waitForChild(c, 20);
    }
    child = -1;
}

}  // namespace echoflow
