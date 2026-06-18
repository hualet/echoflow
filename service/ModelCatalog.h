// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_MODEL_CATALOG_H
#define ECHOFLOW_MODEL_CATALOG_H

#include <filesystem>
#include <string>
#include <vector>

namespace echoflow {

struct ModelEntry {
    std::string id;          // "qwen3-asr-0.6b"  (also the on-disk dir name)
    std::string displayName; // "Qwen3-ASR-0.6B"
    std::string repo;        // "Qwen/Qwen3-ASR-0.6B"
    std::vector<std::string> files;
};

// Ordered: 0.6B first, then 1.7B.
inline const std::vector<ModelEntry>& modelCatalog() {
    static const std::vector<ModelEntry> kCatalog = {
        {"qwen3-asr-0.6b", "Qwen3-ASR-0.6B", "Qwen/Qwen3-ASR-0.6B",
         {"config.json", "generation_config.json", "model.safetensors",
          "vocab.json", "merges.txt"}},
        // Sharded: index + 2 shards; deliberately no single model.safetensors.
        {"qwen3-asr-1.7b", "Qwen3-ASR-1.7B", "Qwen/Qwen3-ASR-1.7B",
         {"config.json", "generation_config.json", "model.safetensors.index.json",
          "model-00001-of-00002.safetensors", "model-00002-of-00002.safetensors",
          "vocab.json", "merges.txt"}},
    };
    return kCatalog;
}

inline const ModelEntry* findModel(const std::string& id) {
    for (const auto& e : modelCatalog()) {
        if (e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

// Required files under dir that are not present. Empty vector == fully present.
inline std::vector<std::string> missingModelFiles(
    const std::filesystem::path& dir, const ModelEntry& e) {
    std::vector<std::string> missing;
    for (const auto& file : e.files) {
        if (!std::filesystem::exists(dir / file)) {
            missing.push_back(file);
        }
    }
    return missing;
}

inline bool isModelPresent(const std::filesystem::path& dir, const ModelEntry& e) {
    return missingModelFiles(dir, e).empty();
}

}  // namespace echoflow

#endif  // ECHOFLOW_MODEL_CATALOG_H
