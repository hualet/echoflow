// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SelfTest.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace echoflow {
namespace fs = std::filesystem;

std::vector<fs::path> modelDirCandidates(const fs::path& modelDir)
{
    if (modelDir.filename() == "model-0.6B") {
        return {modelDir, modelDir.parent_path() / "model"};
    }
    if (modelDir.filename() == "model") {
        return {modelDir, modelDir.parent_path() / "model-0.6B"};
    }
    return {modelDir};
}

fs::path resolveModelDir(const Config& cfg)
{
    for (const auto& candidate : modelDirCandidates(fs::path(cfg.modelDir))) {
        if (fs::exists(candidate)) {
            return candidate;
        }
    }
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

std::vector<std::string> missingModelFiles(const fs::path& modelDir)
{
    std::vector<std::string> missing;
    if (!fs::exists(modelDir)) {
        for (auto* file : kRequiredModelFiles) {
            missing.emplace_back(file);
        }
        return missing;
    }

    for (auto* file : kRequiredModelFiles) {
        if (!fs::exists(modelDir / file)) {
            missing.emplace_back(file);
        }
    }

    bool hasLargeIndex = fs::exists(modelDir / "model.safetensors.index.json");
    bool hasLargeShards = fs::exists(modelDir / "model-00001-of-00002.safetensors")
        && fs::exists(modelDir / "model-00002-of-00002.safetensors");
    if (hasLargeIndex && hasLargeShards) {
        missing.erase(std::remove(missing.begin(), missing.end(), std::string("model.safetensors")),
                      missing.end());
    }

    return missing;
}

static std::string joinMissing(const std::vector<std::string>& missing)
{
    std::ostringstream out;
    for (size_t i = 0; i < missing.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << missing[i];
    }
    return out.str();
}

std::vector<RuntimeCheck> runtimeChecks(const Config& cfg)
{
    auto modelDir = resolveModelDir(cfg);
    auto missing = missingModelFiles(modelDir);
    std::string modelDetail = missing.empty()
        ? modelDir.string()
        : modelDir.string() + " missing: " + joinMissing(missing);

    return {
        {"recordings dir can be created", canCreateDirectory(cfg.recordingsDir), cfg.recordingsDir},
        {"pw-record available", std::system("command -v pw-record >/dev/null 2>&1") == 0, "pw-record"},
        {"model dir exists", fs::exists(modelDir), modelDir.string()},
        {"model files present", missing.empty(), modelDetail},
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
