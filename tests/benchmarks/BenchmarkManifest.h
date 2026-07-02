// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "VadMetrics.h"

#include <QString>

#include <filesystem>
#include <vector>

namespace echoflow {

struct BenchmarkEntry {
    QString id;
    std::filesystem::path audio;
    QString sha256;
    QString reference;
    QString condition;
    std::vector<TimeInterval> speech;
};

std::vector<BenchmarkEntry> loadBenchmarkManifest(const QString& path);

}  // namespace echoflow
