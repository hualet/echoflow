// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SegmentAsrWorker.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <thread>

namespace echoflow {

namespace {

void writeU16(std::ostream& out, uint16_t value)
{
    char bytes[] = {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff),
    };
    out.write(bytes, sizeof(bytes));
}

void writeU32(std::ostream& out, uint32_t value)
{
    char bytes[] = {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff),
        static_cast<char>((value >> 16) & 0xff),
        static_cast<char>((value >> 24) & 0xff),
    };
    out.write(bytes, sizeof(bytes));
}

}  // namespace

struct SegmentAsrWorker::State {
    State(IAsrEngine& asrEngine, std::filesystem::path dir, ResultCallback resultCallback)
        : asr(asrEngine)
        , tempDir(std::move(dir))
        , callback(std::move(resultCallback))
    {
    }

    IAsrEngine& asr;
    std::filesystem::path tempDir;
    ResultCallback callback;

    std::mutex mutex;
    std::condition_variable cv;
    std::deque<QueuedSegment> queue;
    int nextSequence = 0;
    // Cancellation is admission-linearized: once canceled is set under mutex,
    // no new callback is admitted. Already admitted callbacks may still finish
    // so finishAndWait(timeout) is not held hostage by callback code.
    int callbacksInFlight = 0;
    bool accepting = true;
    bool finishing = false;
    bool canceled = false;
    bool processing = false;
    bool drained = true;
};

SegmentAsrWorker::SegmentAsrWorker(IAsrEngine& asr, std::filesystem::path tempDir,
                                   ResultCallback callback)
    : state_(std::make_shared<State>(asr, std::move(tempDir), std::move(callback)))
{
}

SegmentAsrWorker::~SegmentAsrWorker()
{
    cancelAndWait();
}

void SegmentAsrWorker::start()
{
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (thread_.joinable()) {
        return;
    }
    state_->drained = state_->queue.empty();
    thread_ = std::thread(&SegmentAsrWorker::run, state_);
}

void SegmentAsrWorker::enqueue(AudioSegment segment)
{
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (!state_->accepting || state_->canceled) {
            return;
        }
        state_->queue.push_back({state_->nextSequence++, std::move(segment)});
        state_->drained = false;
    }
    state_->cv.notify_all();
}

bool SegmentAsrWorker::finishAndWait(std::chrono::steady_clock::duration timeout)
{
    auto state = state_;
    bool completed = true;
    {
        std::unique_lock<std::mutex> lock(state->mutex);
        state->accepting = false;
        state->finishing = true;
        state->cv.notify_all();
        if (thread_.joinable()) {
            completed = state->cv.wait_for(lock, timeout, [state] {
                return state->drained;
            });
        } else {
            state->drained = state->queue.empty() && !state->processing;
            completed = state->drained;
        }
    }

    if (!completed) {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->canceled = true;
        state->queue.clear();
        state->cv.notify_all();
    }

    if (completed) {
        joinThread();
    } else {
        // IAsrEngine::transcribe() is synchronous and not cancellable. Timeout
        // detaches after moving all thread-touched data into State; the ASR
        // engine object must outlive any detached timed-out transcription.
        detachThread();
    }
    return completed;
}

void SegmentAsrWorker::cancelAndWait()
{
    auto state = state_;
    const bool calledFromWorker = thread_.joinable() && thread_.get_id() == std::this_thread::get_id();
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->accepting = false;
        state->finishing = true;
        state->canceled = true;
        state->queue.clear();
    }
    state->cv.notify_all();
    joinThread();
    if (!calledFromWorker) {
        std::unique_lock<std::mutex> lock(state->mutex);
        state->cv.wait(lock, [state] {
            return state->callbacksInFlight == 0;
        });
    }
}

void SegmentAsrWorker::run(std::shared_ptr<State> state)
{
    while (true) {
        QueuedSegment queued;
        {
            std::unique_lock<std::mutex> lock(state->mutex);
            state->cv.wait(lock, [state] {
                return state->canceled || !state->queue.empty() || state->finishing;
            });
            if (state->canceled) {
                state->queue.clear();
                state->processing = false;
                state->drained = true;
                state->cv.notify_all();
                return;
            }
            if (state->queue.empty()) {
                if (state->finishing) {
                    state->drained = true;
                    state->cv.notify_all();
                    return;
                }
                continue;
            }
            queued = std::move(state->queue.front());
            state->queue.pop_front();
            state->processing = true;
            state->drained = false;
        }

        std::filesystem::path wavPath;
        std::string text;
        try {
            wavPath = writeWav(*state, queued.sequence, queued.segment);
            text = state->asr.transcribe(wavPath);
        } catch (...) {
            text.clear();
        }

        if (!wavPath.empty()) {
            std::error_code error;
            std::filesystem::remove(wavPath, error);
        }

        bool shouldDeliver = false;
        ResultCallback callback;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            shouldDeliver = !state->canceled && !text.empty();
            if (shouldDeliver) {
                callback = state->callback;
                ++state->callbacksInFlight;
            }
        }

        if (shouldDeliver) {
            try {
                callback(queued.sequence, text);
            } catch (...) {
            }
            std::lock_guard<std::mutex> lock(state->mutex);
            --state->callbacksInFlight;
            state->cv.notify_all();
        }

        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->processing = false;
            if (state->queue.empty() && state->finishing) {
                state->drained = true;
                state->cv.notify_all();
            }
        }
    }
}

std::filesystem::path SegmentAsrWorker::writeWav(const State& state, int sequence,
                                                 const AudioSegment& segment)
{
    std::filesystem::create_directories(state.tempDir);

    std::ostringstream name;
    name << "echoflow-segment-" << reinterpret_cast<uintptr_t>(&state) << "-" << sequence << ".wav";
    const std::filesystem::path path = state.tempDir / name.str();

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("failed to open segment wav");
    }

    const uint16_t channels = 1;
    const uint16_t bitsPerSample = 16;
    const uint16_t blockAlign = channels * bitsPerSample / 8;
    const uint32_t sampleRate = static_cast<uint32_t>(segment.sampleRate);
    const uint32_t byteRate = sampleRate * blockAlign;
    const uint32_t dataSize = static_cast<uint32_t>(segment.samples.size() * sizeof(int16_t));

    out.write("RIFF", 4);
    writeU32(out, 36 + dataSize);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    writeU32(out, 16);
    writeU16(out, 1);
    writeU16(out, channels);
    writeU32(out, sampleRate);
    writeU32(out, byteRate);
    writeU16(out, blockAlign);
    writeU16(out, bitsPerSample);
    out.write("data", 4);
    writeU32(out, dataSize);

    for (int16_t sample : segment.samples) {
        writeU16(out, static_cast<uint16_t>(sample));
    }
    if (!out) {
        throw std::runtime_error("failed to write segment wav");
    }
    return path;
}

void SegmentAsrWorker::joinThread()
{
    if (thread_.joinable()) {
        if (thread_.get_id() == std::this_thread::get_id()) {
            thread_.detach();
        } else {
            thread_.join();
        }
    }
}

void SegmentAsrWorker::detachThread()
{
    if (thread_.joinable()) {
        thread_.detach();
    }
}

}  // namespace echoflow
