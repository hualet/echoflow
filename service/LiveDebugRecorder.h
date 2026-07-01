// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_LIVE_DEBUG_RECORDER_H
#define ECHOFLOW_LIVE_DEBUG_RECORDER_H

#include "LiveSegmentCoordinator.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace echoflow {

class LiveDebugRecorder {
public:
    explicit LiveDebugRecorder(std::filesystem::path directory);

    void start();
    void append(const int16_t* samples, size_t count);
    void finish(const LivePipelineMetrics& metrics,
                const std::vector<std::string>& results);

    const std::filesystem::path& wavPath() const;
    const std::filesystem::path& metadataPath() const;

private:
    std::filesystem::path directory_;
    std::filesystem::path wavPath_;
    std::filesystem::path metadataPath_;
    std::vector<int16_t> samples_;
    bool active_ = false;
};

}  // namespace echoflow

#endif
