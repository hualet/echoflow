// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_SELFTEST_H
#define ECHOFLOW_SELFTEST_H

#include "Config.h"

#include <filesystem>
#include <string>
#include <vector>

namespace echoflow {

struct RuntimeCheck
{
    std::string name;
    bool passed;
    std::string detail;
};

std::filesystem::path resolveModelDir(const Config& cfg);
bool canCreateDirectory(const std::filesystem::path& path);

std::vector<RuntimeCheck> runtimeChecks(const Config& cfg);
int runSelfTest(const Config& cfg);

}  // namespace echoflow

#endif
