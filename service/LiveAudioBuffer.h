// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_LIVE_AUDIO_BUFFER_H
#define ECHOFLOW_LIVE_AUDIO_BUFFER_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace echoflow {

class LiveAudioBuffer {
public:
    LiveAudioBuffer();
    // Lifetime contract: callers must call markEof() and ensure all ASR
    // consumers have stopped or joined before destroying this object.
    ~LiveAudioBuffer();

    LiveAudioBuffer(const LiveAudioBuffer&) = delete;
    LiveAudioBuffer& operator=(const LiveAudioBuffer&) = delete;

    void* get();
    const void* get() const;
    void appendS16le(const unsigned char* data, size_t size);
    void appendFloatSamples(const float* samples, int count);
    void markEof();

    int64_t sampleCountForTest() const;
    int eofForTest() const;
    std::vector<float> samplesForTest() const;

private:
    void* live_ = nullptr;
};

}  // namespace echoflow

#endif
