// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "LiveAudioBuffer.h"

#include <climits>
#include <cstring>
#include <new>
#include <vector>

#include <cstdlib>

namespace echoflow {

namespace {

constexpr int64_t kInitialCapacity = 32000;

}  // namespace

LiveAudioBuffer::LiveAudioBuffer()
    : live_(static_cast<qwen_live_audio_t*>(calloc(1, sizeof(qwen_live_audio_t)))) {
    if (!live_) {
        throw std::bad_alloc();
    }

    pthread_mutex_init(&live_->mutex, nullptr);
    pthread_cond_init(&live_->cond, nullptr);
}

LiveAudioBuffer::~LiveAudioBuffer() {
    if (!live_) {
        return;
    }

    markEof();
    pthread_mutex_destroy(&live_->mutex);
    pthread_cond_destroy(&live_->cond);
    free(live_->samples);
    free(live_);
}

qwen_live_audio_t* LiveAudioBuffer::get() const {
    return live_;
}

void LiveAudioBuffer::appendS16le(const unsigned char* data, size_t size) {
    if (!data || size < 2) {
        return;
    }

    const size_t frames = size / 2;
    if (frames > static_cast<size_t>(INT_MAX)) {
        throw std::bad_alloc();
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

    pthread_mutex_lock(&live_->mutex);
    const int64_t needed = live_->n_samples + static_cast<int64_t>(count);
    if (needed > live_->capacity) {
        int64_t newCapacity = live_->capacity > 0 ? live_->capacity : kInitialCapacity;
        while (newCapacity < needed) {
            newCapacity *= 2;
        }

        const auto bytes = static_cast<size_t>(newCapacity) * sizeof(float);
        float* grown = static_cast<float*>(realloc(live_->samples, bytes));
        if (!grown) {
            pthread_mutex_unlock(&live_->mutex);
            throw std::bad_alloc();
        }

        live_->samples = grown;
        live_->capacity = newCapacity;
    }

    memcpy(live_->samples + live_->n_samples, samples, static_cast<size_t>(count) * sizeof(float));
    live_->n_samples += count;
    pthread_cond_signal(&live_->cond);
    pthread_mutex_unlock(&live_->mutex);
}

void LiveAudioBuffer::markEof() {
    pthread_mutex_lock(&live_->mutex);
    live_->eof = 1;
    pthread_cond_signal(&live_->cond);
    pthread_mutex_unlock(&live_->mutex);
}

}  // namespace echoflow
