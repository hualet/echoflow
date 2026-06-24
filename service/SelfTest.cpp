// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SelfTest.h"

#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>

namespace echoflow {
namespace fs = std::filesystem;

fs::path resolveModelDir(const Config& cfg)
{
    return cfg.modelDir;
}

bool canCreateDirectory(const fs::path& path)
{
    fs::path candidate = path;
    while (!fs::exists(candidate) && candidate != candidate.parent_path()) {
        candidate = candidate.parent_path();
    }
    struct stat st {};
    if (stat(candidate.c_str(), &st) != 0) {
        return false;
    }
    return access(candidate.c_str(), W_OK | X_OK) == 0;
}

std::vector<RuntimeCheck> runtimeChecks(const Config& cfg)
{
    const std::string modelPath = cfg.crispModelPath;
    bool modelOk = !modelPath.empty() && fs::exists(modelPath);
    std::string modelDetail = modelOk
        ? modelPath
        : (modelPath.empty() ? std::string("crisp model path not set")
                             : "missing: " + modelPath);

    const std::string crispBinCmd = "command -v " + cfg.crispBinary + " >/dev/null 2>&1";

    return {
        {"recordings dir can be created", canCreateDirectory(cfg.recordingsDir), cfg.recordingsDir},
        {"pw-record available", std::system("command -v pw-record >/dev/null 2>&1") == 0, "pw-record"},
        {"crispasr available", std::system(crispBinCmd.c_str()) == 0, cfg.crispBinary},
        {"crisp model available", modelOk, modelDetail},
        {"control socket path parent", fs::exists(controlSocketPath(cfg).parent_path()),
         controlSocketPath(cfg).string()},
        {"fcitx socket path parent", fs::exists(fcitxSocketPath(cfg).parent_path()),
         fcitxSocketPath(cfg).string()},
        {"ui socket path parent", fs::exists(uiSocketPath(cfg).parent_path()),
         uiSocketPath(cfg).string()},
    };
}

int runSelfTest(const Config& cfg)
{
    bool ok = true;
    for (const auto& check : runtimeChecks(cfg)) {
        std::printf("[%s] %s: %s\n", check.passed ? "OK" : "FAIL",
                    check.name.c_str(), check.detail.c_str());
        ok = ok && check.passed;
    }
    return ok ? 0 : 1;
}

}  // namespace echoflow
