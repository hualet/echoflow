// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SelfTest.h"

#include "ModelCatalog.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace echoflow {
namespace fs = std::filesystem;

namespace {
std::string joinMissing(const std::vector<std::string>& missing)
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
}  // namespace

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
    const fs::path modelDir = resolveModelDir(cfg);
    const ModelEntry* entry = findModel(cfg.modelName);

    std::string modelDetail;
    bool modelOk = false;
    if (modelDir.empty() || entry == nullptr) {
        const std::string display = modelCatalog().front().displayName;
        modelDetail = "未下载 — 打开 EchoFlow 设置 → 模型 下载 " + display;
    } else if (!fs::exists(modelDir)) {
        modelDetail = "目录不存在: " + modelDir.string();
    } else {
        auto missing = missingModelFiles(modelDir, *entry);
        modelOk = missing.empty();
        modelDetail = modelOk ? modelDir.string()
                              : modelDir.string() + " missing: " + joinMissing(missing);
    }

    return {
        {"recordings dir can be created", canCreateDirectory(cfg.recordingsDir), cfg.recordingsDir},
        {"pw-record available", std::system("command -v pw-record >/dev/null 2>&1") == 0, "pw-record"},
        {"model available", modelOk, modelDetail},
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
