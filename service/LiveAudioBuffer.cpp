// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "LiveAudioBuffer.h"

#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

#include <cstdlib>

extern "C" {
#include "qwen_asr.h"
}

namespace echoflow {

namespace {

constexpr int64_t kInitialCapacity = 32000;

qwen_live_audio_t* liveFrom(void* ptr) {
    return static_cast<qwen_live_audio_t*>(ptr);
}

const qwen_live_audio_t* liveFrom(const void* ptr) {
    return static_cast<const qwen_live_audio_t*>(ptr);
}

}  // namespace

LiveAudioBuffer::LiveAudioBuffer()
    : live_(calloc(1, sizeof(qwen_live_audio_t))) {
    if (!live_) {
        throw std::bad_alloc();
    }

    qwen_live_audio_t* live = liveFrom(live_);
    const int mutexResult = pthread_mutex_init(&live->mutex, nullptr);
    if (mutexResult != 0) {
        free(live_);
        live_ = nullptr;
        throw std::runtime_error("failed to initialize live audio mutex");
    }

    const int condResult = pthread_cond_init(&live->cond, nullptr);
    if (condResult != 0) {
        pthread_mutex_destroy(&live->mutex);
        free(live_);
        live_ = nullptr;
        throw std::runtime_error("failed to initialize live audio condition variable");
    }
}

LiveAudioBuffer::~LiveAudioBuffer() {
    if (!live_) {
        return;
    }

    markEof();
    qwen_live_audio_t* live = liveFrom(live_);
    pthread_mutex_destroy(&live->mutex);
    pthread_cond_destroy(&live->cond);
    free(live->samples);
    free(live_);
}

void* LiveAudioBuffer::get() {
    return live_;
}

const void* LiveAudioBuffer::get() const {
    return live_;
}

void LiveAudioBuffer::appendS16le(const unsigned char* data, size_t size) {
    if (!data || size < 2) {
        return;
    }

    const size_t frames = size / 2;
    if (frames > static_cast<size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("too many s16le frames for live audio buffer");
    }

    std::vector<float> samples(frames);
    for (size_t i = 0; i < frames; ++i) {
        const unsigned int raw = static_cast<unsigned int>(data[i * 2])
                                 | (static_cast<unsigned int>(data[i * 2 + 1]) << 8);
        const int value = raw >= 0x8000U ? static_cast<int>(raw) - 0x10000 : static_cast<int>(raw);
        samples[i] = static_cast<float>(value) / 32768.0f;
    }

    appendFloatSamples(samples.data(), static_cast<int>(samples.size()));
}

void LiveAudioBuffer::appendFloatSamples(const float* samples, int count) {
    if (!samples || count <= 0) {
        return;
    }

    qwen_live_audio_t* live = liveFrom(live_);
    pthread_mutex_lock(&live->mutex);
    if (live->n_samples > std::numeric_limits<int64_t>::max() - static_cast<int64_t>(count)) {
        pthread_mutex_unlock(&live->mutex);
        throw std::length_error("live audio sample count overflow");
    }

    const int64_t needed = live->n_samples + static_cast<int64_t>(count);
    if (needed > live->capacity) {
        int64_t newCapacity = live->capacity > 0 ? live->capacity : kInitialCapacity;
        while (newCapacity < needed) {
            if (newCapacity > std::numeric_limits<int64_t>::max() / 2) {
                pthread_mutex_unlock(&live->mutex);
                throw std::length_error("live audio capacity overflow");
            }
            newCapacity *= 2;
        }

        if (static_cast<uint64_t>(newCapacity)
            > static_cast<uint64_t>(std::numeric_limits<size_t>::max() / sizeof(float))) {
            pthread_mutex_unlock(&live->mutex);
            throw std::length_error("live audio allocation size overflow");
        }

        const auto bytes = static_cast<size_t>(newCapacity) * sizeof(float);
        float* grown = static_cast<float*>(realloc(live->samples, bytes));
        if (!grown) {
            pthread_mutex_unlock(&live->mutex);
            throw std::bad_alloc();
        }

        live->samples = grown;
        live->capacity = newCapacity;
    }

    memcpy(live->samples + live->n_samples, samples, static_cast<size_t>(count) * sizeof(float));
    live->n_samples += count;
    pthread_cond_signal(&live->cond);
    pthread_mutex_unlock(&live->mutex);
}

void LiveAudioBuffer::markEof() {
    qwen_live_audio_t* live = liveFrom(live_);
    pthread_mutex_lock(&live->mutex);
    live->eof = 1;
    pthread_cond_signal(&live->cond);
    pthread_mutex_unlock(&live->mutex);
}

int64_t LiveAudioBuffer::sampleCountForTest() const {
    const qwen_live_audio_t* live = liveFrom(live_);
    pthread_mutex_lock(const_cast<pthread_mutex_t*>(&live->mutex));
    const int64_t count = live->n_samples;
    pthread_mutex_unlock(const_cast<pthread_mutex_t*>(&live->mutex));
    return count;
}

int LiveAudioBuffer::eofForTest() const {
    const qwen_live_audio_t* live = liveFrom(live_);
    pthread_mutex_lock(const_cast<pthread_mutex_t*>(&live->mutex));
    const int eof = live->eof;
    pthread_mutex_unlock(const_cast<pthread_mutex_t*>(&live->mutex));
    return eof;
}

std::vector<float> LiveAudioBuffer::samplesForTest() const {
    const qwen_live_audio_t* live = liveFrom(live_);
    pthread_mutex_lock(const_cast<pthread_mutex_t*>(&live->mutex));
    const int64_t count = live->n_samples;
    std::vector<float> samples;
    if (count > 0) {
        samples.assign(live->samples, live->samples + count);
    }
    pthread_mutex_unlock(const_cast<pthread_mutex_t*>(&live->mutex));
    return samples;
}

}  // namespace echoflow
