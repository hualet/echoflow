// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_RECORDER_H
#define ECHOFLOW_RECORDER_H

#include "Config.h"
#include "Interfaces.h"

#include <chrono>
#include <filesystem>
#include <sys/types.h>

namespace echoflow {

class PipeWireRecorder : public IRecorder {
public:
    explicit PipeWireRecorder(Config cfg);
    void start() override;
    std::filesystem::path stop() override;

private:
    Config cfg_;
    pid_t child_ = -1;
    std::filesystem::path path_;
    std::chrono::steady_clock::time_point startedAt_;
};

}  // namespace echoflow

#endif
