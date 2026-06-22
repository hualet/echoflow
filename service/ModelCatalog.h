// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_MODEL_CATALOG_H
#define ECHOFLOW_MODEL_CATALOG_H

#include <filesystem>
#include <string>
#include <vector>

namespace echoflow {

struct ModelEntry {
    struct File {
        std::string path;
        // Empty means use ModelEntry::repo.
        std::string repo;
    };

    std::string id;          // Also the on-disk dir name.
    std::string displayName;
    std::string repo;
    std::vector<File> files;
};

// Ordered: SenseVoice first for this branch's default experience, then Qwen fallback models.
inline const std::vector<ModelEntry>& modelCatalog() {
    static const std::vector<ModelEntry> kCatalog = {
        {"sensevoice-small-q8", "SenseVoiceSmall Q8", "FunAudioLLM/SenseVoiceSmall-GGUF",
         {{"sensevoice-small-q8.gguf", ""},
          {"fsmn-vad.gguf", "FunAudioLLM/fsmn-vad-GGUF"}}},
        {"qwen3-asr-0.6b", "Qwen3-ASR-0.6B", "Qwen/Qwen3-ASR-0.6B",
         {{"config.json", ""}, {"generation_config.json", ""}, {"model.safetensors", ""},
          {"vocab.json", ""}, {"merges.txt", ""}}},
        // Sharded: index + 2 shards; deliberately no single model.safetensors.
        {"qwen3-asr-1.7b", "Qwen3-ASR-1.7B", "Qwen/Qwen3-ASR-1.7B",
         {{"config.json", ""}, {"generation_config.json", ""},
          {"model.safetensors.index.json", ""},
          {"model-00001-of-00002.safetensors", ""},
          {"model-00002-of-00002.safetensors", ""},
          {"vocab.json", ""}, {"merges.txt", ""}}},
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

inline bool isSenseVoiceModel(const std::string& id) {
    return id == "sensevoice-small-q8";
}

// Required files under dir that are not present. Empty vector == fully present.
inline std::vector<std::string> missingModelFiles(
    const std::filesystem::path& dir, const ModelEntry& e) {
    std::vector<std::string> missing;
    for (const auto& file : e.files) {
        if (!std::filesystem::exists(dir / file.path)) {
            missing.push_back(file.path);
        }
    }
    return missing;
}

inline bool isModelPresent(const std::filesystem::path& dir, const ModelEntry& e) {
    return missingModelFiles(dir, e).empty();
}

}  // namespace echoflow

#endif  // ECHOFLOW_MODEL_CATALOG_H
