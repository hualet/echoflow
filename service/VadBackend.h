// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_VAD_BACKEND_H
#define ECHOFLOW_VAD_BACKEND_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace echoflow {

struct SpeechInterval {
    size_t beginSample = 0;
    size_t endSample = 0;
    float confidence = 0.0f;
};

class IVadBackend {
public:
    virtual ~IVadBackend() = default;
    virtual std::vector<SpeechInterval> detect(const int16_t* pcm, size_t count) = 0;
    virtual std::string name() const = 0;
};

}  // namespace echoflow

#endif
