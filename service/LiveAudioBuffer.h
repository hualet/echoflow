// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_LIVE_AUDIO_BUFFER_H
#define ECHOFLOW_LIVE_AUDIO_BUFFER_H

extern "C" {
#include "qwen_asr.h"
}

#include <cstddef>

namespace echoflow {

class LiveAudioBuffer {
public:
    LiveAudioBuffer();
    ~LiveAudioBuffer();

    LiveAudioBuffer(const LiveAudioBuffer&) = delete;
    LiveAudioBuffer& operator=(const LiveAudioBuffer&) = delete;

    qwen_live_audio_t* get() const;
    void appendS16le(const unsigned char* data, size_t size);
    void appendFloatSamples(const float* samples, int count);
    void markEof();

private:
    qwen_live_audio_t* live_ = nullptr;
};

}  // namespace echoflow

#endif
