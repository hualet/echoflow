// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Config.h"

#include <cctype>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/types.h>
#include <unistd.h>

namespace echoflow {
namespace fs = std::filesystem;

namespace {

std::string trim(const std::string& value)
{
    auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool parseBool(const std::string& value)
{
    std::string lower;
    lower.reserve(value.size());
    for (unsigned char c : value) {
        lower += static_cast<char>(std::tolower(c));
    }
    return lower == "true" || lower == "1" || lower == "yes" || lower == "on";
}

}  // namespace

Config Config::defaultConfig()
{
    const char* home = std::getenv("HOME");
    std::string h = home ? home : "/tmp";

    Config c;
    c.recordingsDir = h + "/.local/share/echoflow/recordings";
    return c;
}

std::string normalizeModelName(const std::string& value)
{
    if (value == "qwen-asr-0.6b" || value == "0.6b" || value == "0.6B") {
        return "qwen3-asr-0.6b";
    }
    if (value == "qwen-asr-1.7b" || value == "1.7b" || value == "1.7B") {
        return "qwen3-asr-1.7b";
    }
    return value;
}

std::string expandPath(const std::string& value, const fs::path& baseDir)
{
    std::string s = value;
    const char* home = std::getenv("HOME");
    std::string homeValue = home ? home : "/";

    const std::string token = "$HOME";
    for (size_t pos = 0; (pos = s.find(token, pos)) != std::string::npos; pos += homeValue.size()) {
        s.replace(pos, token.size(), homeValue);
    }
    if (!s.empty() && s[0] == '~') {
        s.replace(0, 1, homeValue);
    }

    fs::path p(s);
    if (!p.is_absolute()) {
        p = baseDir / p;
    }
    return p.string();
}

fs::path runtimeDir()
{
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg && access(xdg, W_OK | X_OK) == 0) {
        return xdg;
    }

    fs::path userRuntime = fs::path("/run/user") / std::to_string(getuid());
    if (fs::exists(userRuntime)) {
        return userRuntime;
    }
    return "/tmp";
}

fs::path controlSocketPath(const Config&)
{
    return runtimeDir() / "echoflow-control.sock";
}

fs::path fcitxSocketPath(const Config&)
{
    return runtimeDir() / "echoflow-fcitx.sock";
}

fs::path uiSocketPath(const Config&)
{
    return runtimeDir() / "echoflow-ui.sock";
}

std::string stripPunctuation(const std::string& text)
{
    static const char* kTrail = "。．.，,、！？!?；;：:\n\r\t ";
    auto end = text.find_last_not_of(kTrail);
    if (end == std::string::npos) {
        return {};
    }
    return text.substr(0, end + 1);
}

Config loadDtkConf(const fs::path& path)
{
    Config cfg = Config::defaultConfig();
    std::ifstream in(path);
    if (!in) {
        return cfg;
    }

    std::string line;
    std::string section;
    while (std::getline(in, line)) {
        auto hash = line.find('#');
        if (hash != std::string::npos) {
            line.erase(hash);
        }

        std::string stripped = trim(line);
        if (stripped.empty()) {
            continue;
        }
        if (stripped.front() == '[') {
            auto close = stripped.find(']');
            section = close == std::string::npos ? std::string() : stripped.substr(1, close - 1);
            continue;
        }
        if (section.empty()) {
            continue;
        }

        auto eq = stripped.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = trim(stripped.substr(0, eq));
        if (key != "value") {
            continue;
        }
        std::string val = trim(stripped.substr(eq + 1));

        if (section == "basic.model.model_name") {
            cfg.modelName = val;
        } else if (section == "basic.recognition.language") {
            cfg.language = val.empty() ? std::optional<std::string>{} : std::optional<std::string>(val);
        } else if (section == "basic.recognition.prompt") {
            cfg.prompt = val;
        } else if (section == "basic.recognition.strip_trailing_punctuation") {
            cfg.stripTrailingPunctuation = parseBool(val);
        } else if (section == "basic.recording.min_record_seconds") {
            cfg.minRecordSeconds = std::stod(val);
        } else if (section == "basic.recording.rate") {
            cfg.pwRecord.rate = std::stoi(val);
        } else if (section == "basic.recording.channels") {
            cfg.pwRecord.channels = std::stoi(val);
        } else if (section == "basic.recording.format") {
            cfg.pwRecord.format = val;
        } else if (section == "basic.recording.source") {
            cfg.pwRecord.source = val;
        } else if (section == "advanced.runtime.asr_timeout_seconds") {
            cfg.asrTimeoutSeconds = std::stoi(val);
        } else if (section == "advanced.runtime.openblas_threads") {
            cfg.openBlasThreads = std::max(1, std::stoi(val));
        } else if (section == "advanced.fcitx.fcitx_commit") {
            cfg.fcitxCommit = parseBool(val);
        } else if (section == "advanced.storage.recordings_dir") {
            cfg.recordingsDir = val;
        }
    }

    fs::path base = path.parent_path();
    cfg.recordingsDir = expandPath(cfg.recordingsDir, base);
    cfg.modelName = normalizeModelName(cfg.modelName);
    cfg.modelDir = (base / cfg.modelName).string();
    return cfg;
}

}  // namespace echoflow
