# In-app model download — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove `scripts/setup-qwen-asr-0.6b.sh` and let users download the Qwen3-ASR 0.6B / 1.7B safetensors models from the EchoFlow settings dialog (two DTK rows: model name left, download button + progress right), into the EchoFlow config directory.

**Architecture:** Download lives entirely in `ui-host` (Qt6 Network). A header-only `service/ModelCatalog.h` is the single source of truth for model id / display name / HF repo / file list, shared by `ui-host` (what to fetch) and `service/SelfTest` (what to verify). `Config::modelDir` is derived from `model_name` + the config-file directory; the free-text `model_dir` setting is removed.

**Tech Stack:** C++17, CMake, Qt6 (Core/Gui/Qml/Quick/Widgets/**Network**), DTK6 (Widget/Core/Gui), QTest, bash spec-as-test.

**Spec:** `docs/superpowers/specs/2026-06-18-in-app-model-download-design.md`

**Refinement note (vs spec §"DTK custom widget factory"):** DTK6's `DSettingsWidgetFactory::registerWidget` passes the handler a `QObject*` (confirmed in `/usr/include/dtk6/DWidget/dsettingswidgetfactory.h`), and population of `option->data()` from the schema JSON is not guaranteed. The factory therefore maps `option->key()` → `ModelEntry` via an explicit code-side table (the spec's accepted fallback). The schema's per-option `data.model_id` field is omitted.

---

## File structure

**Create:**
- `service/ModelCatalog.h` — header-only catalog + `missingModelFiles`/`isModelPresent`.
- `ui-host/ModelDownloader.h` / `.cpp` — Qt6 Network downloader, `.part` + progress.
- `ui-host/ModelRowWidget.h` / `.cpp` — one row: status label + button, owns a downloader.
- `tests/test_model_catalog.cpp` — QTest for the catalog.

**Modify:**
- `service/Config.h` / `.cpp` — add `normalizeModelName`, derive `modelDir`, drop `model_dir` parsing + `legacyDefaultModelDir` + `expandPath` on modelDir; `modelName` default → `qwen3-asr-0.6b`.
- `service/SelfTest.h` / `.cpp` — use `ModelCatalog`; drop `kRequiredModelFiles`, `modelDirCandidates`, single-arg `missingModelFiles`; `resolveModelDir` trivial; actionable "未下载" message.
- `service/main.cpp` — `--print-default-config` no longer prints a concrete `model_dir`.
- `ui-host/settings-schema.json` — `model_name` values, two `modeldownload` rows, `mirror` combobox; remove `model_dir`.
- `ui-host/EchoFlowSettings.cpp` — combobox items + default-write list.
- `ui-host/SettingsDialog.cpp` — register the `modeldownload` widget factory.
- `ui-host/CMakeLists.txt` — add new files, `Qt6::Network`, `service/` include dir.
- `install-user.sh` — default config block (drop `model_dir`, add `mirror`).
- `tests/CMakeLists.txt` — add `test_model_catalog`.
- `tests/test_config.cpp`, `tests/test_selftest.cpp` — update expectations.
- `tests/spec/run_spec.sh` — flip script assertions; assert new schema/install contents.
- `README.md`, `AGENTS.md` — docs.

**Delete:**
- `scripts/setup-qwen-asr-0.6b.sh` (and `scripts/` if empty).

**Ordering rationale:** Catalog first (no deps) → Config → SelfTest (needs catalog) → service CLI → schema/settings → install-user.sh → ui-host downloader/widget/factory → script removal → spec test → docs → full verify. Each task builds and tests green before the next.

**Every C++ file created or modified keeps:**
`SPDX-FileCopyrightText: 2026 Hualet Wang` and `SPDX-License-Identifier: GPL-3.0-or-later`.

---

## Task 1: ModelCatalog (header-only) + tests

**Files:**
- Create: `service/ModelCatalog.h`
- Create: `tests/test_model_catalog.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create the catalog header**

Create `service/ModelCatalog.h`:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_MODEL_CATALOG_H
#define ECHOFLOW_MODEL_CATALOG_H

#include <algorithm>
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

// Required files under dir that are not present. A 1.7B shard pair
// (model-00001-of-00002.safetensors + model-00002-of-00002.safetensors)
// satisfies the single-shard requirement. Empty vector == fully present.
inline std::vector<std::string> missingModelFiles(
    const std::filesystem::path& dir, const ModelEntry& e) {
    std::vector<std::string> missing;
    for (const auto& file : e.files) {
        if (!std::filesystem::exists(dir / file)) {
            missing.push_back(file);
        }
    }
    bool needsSingleShard = std::find(e.files.begin(), e.files.end(),
                                      std::string("model.safetensors")) != e.files.end();
    if (needsSingleShard) {
        bool hasIndex = std::filesystem::exists(dir / "model.safetensors.index.json");
        bool hasShards =
            std::filesystem::exists(dir / "model-00001-of-00002.safetensors") &&
            std::filesystem::exists(dir / "model-00002-of-00002.safetensors");
        if (hasIndex && hasShards) {
            missing.erase(std::remove(missing.begin(), missing.end(),
                                      std::string("model.safetensors")),
                          missing.end());
        }
    }
    return missing;
}

inline bool isModelPresent(const std::filesystem::path& dir, const ModelEntry& e) {
    return missingModelFiles(dir, e).empty();
}

}  // namespace echoflow

#endif  // ECHOFLOW_MODEL_CATALOG_H
```

- [ ] **Step 2: Write the failing test**

Create `tests/test_model_catalog.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "ModelCatalog.h"

#include <algorithm>
#include <fstream>

using namespace echoflow;

class TestModelCatalog : public QObject {
    Q_OBJECT

private slots:
    void catalogHasTwoModels();
    void findModelReturnsNullForUnknown();
    void missingFilesListsAbsent();
    void presentWhenAllFilesExist();
    void largeShardsSatisfySingleShard();
    void largeMissingOneShardIsMissing();
};

void TestModelCatalog::catalogHasTwoModels() {
    auto& c = modelCatalog();
    QCOMPARE(c.size(), size_t(2));
    QCOMPARE(QString::fromStdString(c[0].id), QStringLiteral("qwen3-asr-0.6b"));
    QCOMPARE(QString::fromStdString(c[1].id), QStringLiteral("qwen3-asr-1.7b"));
}

void TestModelCatalog::findModelReturnsNullForUnknown() {
    QVERIFY(findModel("qwen3-asr-0.6b") != nullptr);
    QVERIFY(findModel("qwen3-asr-1.7b") != nullptr);
    QVERIFY(findModel("nope") == nullptr);
    QVERIFY(findModel("") == nullptr);
}

void TestModelCatalog::missingFilesListsAbsent() {
    QTemporaryDir dir;
    auto modelDir = std::filesystem::path(dir.path().toStdString());
    std::ofstream(modelDir / "config.json").put('x');

    const ModelEntry* e = findModel("qwen3-asr-0.6b");
    QVERIFY(e != nullptr);
    auto missing = missingModelFiles(modelDir, *e);
    QCOMPARE(missing.size(), size_t(4));
    QVERIFY(std::find(missing.begin(), missing.end(), std::string("model.safetensors")) != missing.end());
    QVERIFY(std::find(missing.begin(), missing.end(), std::string("vocab.json")) != missing.end());
}

void TestModelCatalog::presentWhenAllFilesExist() {
    QTemporaryDir dir;
    auto modelDir = std::filesystem::path(dir.path().toStdString());
    const ModelEntry* e = findModel("qwen3-asr-0.6b");
    for (const auto& f : e->files) {
        std::ofstream(modelDir / f).put('x');
    }
    QVERIFY(isModelPresent(modelDir, *e));
    QVERIFY(missingModelFiles(modelDir, *e).empty());
}

void TestModelCatalog::largeShardsSatisfySingleShard() {
    QTemporaryDir dir;
    auto modelDir = std::filesystem::path(dir.path().toStdString());
    const ModelEntry* e = findModel("qwen3-asr-1.7b");
    for (const auto& f : e->files) {
        std::ofstream(modelDir / f).put('x');
    }
    QVERIFY(isModelPresent(modelDir, *e));
}

void TestModelCatalog::largeMissingOneShardIsMissing() {
    QTemporaryDir dir;
    auto modelDir = std::filesystem::path(dir.path().toStdString());
    const ModelEntry* e = findModel("qwen3-asr-1.7b");
    for (const auto& f : e->files) {
        std::ofstream(modelDir / f).put('x');
    }
    std::filesystem::remove(modelDir / "model-00002-of-00002.safetensors");
    QVERIFY(!isModelPresent(modelDir, *e));
}

QTEST_GUILESS_MAIN(TestModelCatalog)
#include "test_model_catalog.moc"
```

- [ ] **Step 3: Register the test target**

In `tests/CMakeLists.txt`, add `test_model_catalog` to the list:

```cmake
set(ECHOFLOW_TESTS
    test_asr_engine
    test_config
    test_voice_session
    test_committer
    test_selftest
    test_model_catalog)
```

(The `foreach` loop already builds + links `echoflow_service Qt6::Test pthread` and registers `add_test` for each entry, so no other change is needed. `ModelCatalog.h` is reached via `echoflow_service`'s PUBLIC include of `service/`.)

- [ ] **Step 4: Run the test, expect PASS**

```bash
cmake --build build && ctest --test-dir build -R test_model_catalog --output-on-failure
```

Expected: `test_model_catalog ... Passed` (6 test slots).

- [ ] **Step 5: Commit**

```bash
git add service/ModelCatalog.h tests/test_model_catalog.cpp tests/CMakeLists.txt
git commit -m "Add ModelCatalog header with 0.6B/1.7B file lists"
```

---

## Task 2: Config — derive modelDir, normalize model_name

**Files:**
- Modify: `service/Config.h`
- Modify: `service/Config.cpp`
- Modify: `tests/test_config.cpp`

- [ ] **Step 1: Write the failing tests**

Replace the body of `tests/test_config.cpp` with:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "Config.h"

using namespace echoflow;

class TestConfig : public QObject {
    Q_OBJECT

private slots:
    void defaultConfigHasExpectedFields();
    void expandPathResolvesHome();
    void normalizeModelNameMapsVariants();
    void loadDtkConfDerivesModelDirFromName();
    void loadDtkConfNormalizesLegacyModelName();
    void loadDtkConfIgnoresModelDirKey();
    void loadDtkConfIgnoresUnknownSections();
};

void TestConfig::defaultConfigHasExpectedFields() {
    Config c = Config::defaultConfig();
    QCOMPARE(QString::fromStdString(c.modelName), QStringLiteral("qwen3-asr-0.6b"));
    QCOMPARE(c.pwRecord.rate, 16000);
    QCOMPARE(QString::fromStdString(c.pwRecord.format), QStringLiteral("s16"));
    QCOMPARE(c.fcitxCommit, true);
    QVERIFY(c.language.has_value());
    QCOMPARE(QString::fromStdString(*c.language), QStringLiteral("Chinese"));
    QVERIFY(c.modelDir.empty());
}

void TestConfig::expandPathResolvesHome() {
    setenv("HOME", "/tmp/fakehome", 1);
    std::filesystem::path base("/home/u/.config/echoflow");
    QCOMPARE(QString::fromStdString(expandPath("$HOME/AI/Model/qwen3-asr-0.6b", base)),
             QStringLiteral("/tmp/fakehome/AI/Model/qwen3-asr-0.6b"));
    QCOMPARE(QString::fromStdString(expandPath("recordings", base)),
             QStringLiteral("/home/u/.config/echoflow/recordings"));
}

void TestConfig::normalizeModelNameMapsVariants() {
    QCOMPARE(QString::fromStdString(normalizeModelName("qwen-asr-0.6b")), QStringLiteral("qwen3-asr-0.6b"));
    QCOMPARE(QString::fromStdString(normalizeModelName("0.6b")), QStringLiteral("qwen3-asr-0.6b"));
    QCOMPARE(QString::fromStdString(normalizeModelName("0.6B")), QStringLiteral("qwen3-asr-0.6b"));
    QCOMPARE(QString::fromStdString(normalizeModelName("1.7B")), QStringLiteral("qwen3-asr-1.7b"));
    // Unknown / empty are returned unchanged (no invented default).
    QCOMPARE(QString::fromStdString(normalizeModelName("something-else")), QStringLiteral("something-else"));
    QCOMPARE(QString::fromStdString(normalizeModelName("")), QStringLiteral(""));
}

void TestConfig::loadDtkConfDerivesModelDirFromName() {
    QTemporaryFile f;
    QVERIFY(f.open());
    f.write("[basic.model.model_name]\nvalue=qwen3-asr-1.7b\n"
            "[basic.recognition.language]\nvalue=English\n"
            "[basic.recognition.prompt]\nvalue=Preserve spelling: CUDA\n"
            "[basic.recording.rate]\nvalue=22050\n"
            "[basic.recording.min_record_seconds]\nvalue=0.5\n"
            "[basic.recognition.strip_trailing_punctuation]\nvalue=true\n"
            "[advanced.fcitx.fcitx_commit]\nvalue=false\n");
    f.close();

    Config c = loadDtkConf(f.fileName().toStdString());
    QCOMPARE(QString::fromStdString(c.modelName), QStringLiteral("qwen3-asr-1.7b"));
    QCOMPARE(QString::fromStdString(*c.language), QStringLiteral("English"));
    QCOMPARE(QString::fromStdString(c.prompt), QStringLiteral("Preserve spelling: CUDA"));
    QCOMPARE(c.pwRecord.rate, 22050);
    QCOMPARE(c.minRecordSeconds, 0.5);
    QCOMPARE(c.stripTrailingPunctuation, true);
    QCOMPARE(c.fcitxCommit, false);
    QCOMPARE(QString::fromStdString(c.modelDir),
             QString::fromStdString((std::filesystem::path(f.fileName().toStdString()).parent_path()
                                     / "qwen3-asr-1.7b").string()));
}

void TestConfig::loadDtkConfNormalizesLegacyModelName() {
    QTemporaryFile f;
    QVERIFY(f.open());
    f.write("[basic.model.model_name]\nvalue=qwen-asr-0.6b\n");
    f.close();

    Config c = loadDtkConf(f.fileName().toStdString());
    QCOMPARE(QString::fromStdString(c.modelDir),
             QString::fromStdString((std::filesystem::path(f.fileName().toStdString()).parent_path()
                                     / "qwen3-asr-0.6b").string()));
}

void TestConfig::loadDtkConfIgnoresModelDirKey() {
    QTemporaryFile f;
    QVERIFY(f.open());
    f.write("[basic.model.model_name]\nvalue=qwen3-asr-0.6b\n"
            "[advanced.runtime.model_dir]\nvalue=$HOME/AI/Model/should-be-ignored\n");
    f.close();

    Config c = loadDtkConf(f.fileName().toStdString());
    // modelDir is derived purely from model_name + config dir, never from the key.
    QCOMPARE(QString::fromStdString(c.modelDir),
             QString::fromStdString((std::filesystem::path(f.fileName().toStdString()).parent_path()
                                     / "qwen3-asr-0.6b").string()));
    QVERIFY(c.modelDir.find("should-be-ignored") == std::string::npos);
}

void TestConfig::loadDtkConfIgnoresUnknownSections() {
    QTemporaryFile f;
    QVERIFY(f.open());
    f.write("[some.unknown.thing]\nvalue=ignored\n"
            "[basic.model.model_name]\nvalue=qwen3-asr-0.6b\n");
    f.close();

    Config c = loadDtkConf(f.fileName().toStdString());
    QCOMPARE(QString::fromStdString(c.modelName), QStringLiteral("qwen3-asr-0.6b"));
}

QTEST_GUILESS_MAIN(TestConfig)
#include "test_config.moc"
```

- [ ] **Step 2: Run tests, expect compile failure**

```bash
cmake --build build 2>&1 | head -40
```

Expected: failure — `normalizeModelName` not declared; `defaultConfig().modelDir` no longer empty; legacy-migration test references removed symbols.

- [ ] **Step 3: Update Config.h**

In `service/Config.h`:
- Change `std::string modelName = "qwen-asr-0.6b";` → `std::string modelName = "qwen3-asr-0.6b";`
- Add a declaration after the `Config` block (next to `expandPath`):

```cpp
std::string normalizeModelName(const std::string& value);
```

- [ ] **Step 4: Update Config.cpp**

(a) Delete the whole `legacyDefaultModelDir()` function (anonymous namespace).

(b) `Config::defaultConfig()` becomes (note: no `modelDir` assignment):

```cpp
Config Config::defaultConfig()
{
    const char* home = std::getenv("HOME");
    std::string h = home ? home : "/tmp";

    Config c;
    c.recordingsDir = h + "/.local/share/echoflow/recordings";
    return c;
}
```

(c) Add `normalizeModelName` (place it above `loadDtkConf`):

```cpp
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
```

(d) In `loadDtkConf`, delete the `else if (section == "advanced.runtime.model_dir") { cfg.modelDir = val; }` branch.

(e) Replace the tail of `loadDtkConf` — specifically remove these two lines:
```cpp
    cfg.modelDir = expandPath(cfg.modelDir, base);
    if (cfg.modelDir == legacyDefaultModelDir()) {
        cfg.modelDir = Config::defaultConfig().modelDir;
    }
```
and the `return cfg;` stays. The end of `loadDtkConf` becomes:

```cpp
    fs::path base = path.parent_path();
    cfg.recordingsDir = expandPath(cfg.recordingsDir, base);
    cfg.modelDir = (base / normalizeModelName(cfg.modelName)).string();
    return cfg;
```

- [ ] **Step 5: Run tests, expect PASS**

```bash
cmake --build build && ctest --test-dir build -R "test_config|test_model_catalog" --output-on-failure
```

Expected: both pass.

- [ ] **Step 6: Commit**

```bash
git add service/Config.h service/Config.cpp tests/test_config.cpp
git commit -m "Derive model_dir from model_name; drop model_dir config key"
```

---

## Task 3: SelfTest — use ModelCatalog, actionable message

**Files:**
- Modify: `service/SelfTest.h`
- Modify: `service/SelfTest.cpp`
- Modify: `tests/test_selftest.cpp`

- [ ] **Step 1: Rewrite the test**

Replace `tests/test_selftest.cpp` entirely:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "SelfTest.h"

#include <algorithm>
#include <fstream>

using namespace echoflow;

class TestSelfTest : public QObject {
    Q_OBJECT

private slots:
    void resolveModelDirIsTrivial();
    void canCreateDirectoryOnTmp();
    void runtimeChecksReportsMissingModelFiles();
    void runtimeChecksActionableWhenModelDirEmpty();
};

void TestSelfTest::resolveModelDirIsTrivial() {
    Config c;
    c.modelDir = "/some/derived/path";
    QCOMPARE(resolveModelDir(c), std::filesystem::path("/some/derived/path"));
}

void TestSelfTest::canCreateDirectoryOnTmp() {
    QTemporaryDir dir;
    QVERIFY(canCreateDirectory(std::filesystem::path(dir.path().toStdString()) / "sub/deep"));
}

void TestSelfTest::runtimeChecksReportsMissingModelFiles() {
    QTemporaryDir dir;
    auto modelDir = std::filesystem::path(dir.path().toStdString());
    std::ofstream(modelDir / "config.json").put('x');

    Config c;
    c.modelName = "qwen3-asr-0.6b";
    c.modelDir = modelDir.string();

    auto checks = runtimeChecks(c);
    auto it = std::find_if(checks.begin(), checks.end(),
                           [](const RuntimeCheck& r) { return r.name == "model available"; });
    QVERIFY(it != checks.end());
    QVERIFY(!it->passed);
    QVERIFY(it->detail.find("model.safetensors") != std::string::npos);
    QVERIFY(it->detail.find("vocab.json") != std::string::npos);
}

void TestSelfTest::runtimeChecksActionableWhenModelDirEmpty() {
    Config c;
    c.modelName = "qwen3-asr-0.6b";
    c.modelDir.clear();

    auto checks = runtimeChecks(c);
    auto it = std::find_if(checks.begin(), checks.end(),
                           [](const RuntimeCheck& r) { return r.name == "model available"; });
    QVERIFY(it != checks.end());
    QVERIFY(!it->passed);
    QVERIFY(it->detail.find("未下载") != std::string::npos);
    QVERIFY(it->detail.find("Qwen3-ASR-0.6B") != std::string::npos);
}

QTEST_GUILESS_MAIN(TestSelfTest)
#include "test_selftest.moc"
```

- [ ] **Step 2: Run, expect compile failure**

```bash
cmake --build build 2>&1 | head -40
```

Expected: `kRequiredModelFiles`, `modelDirCandidates`, single-arg `missingModelFiles` missing or signature mismatch.

- [ ] **Step 3: Update SelfTest.h**

Replace the file body so it reads:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_SELFTEST_H
#define ECHOFLOW_SELFTEST_H

#include "Config.h"
#include "ModelCatalog.h"

#include <filesystem>
#include <string>
#include <vector>

namespace echoflow {

struct RuntimeCheck {
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
```

(The file-list helpers `missingModelFiles`/`isModelPresent` now live in `ModelCatalog.h`, included above.)

- [ ] **Step 4: Update SelfTest.cpp**

Replace the file so it reads:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SelfTest.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace echoflow {
namespace fs = std::filesystem;

namespace {
std::string joinMissing(const std::vector<std::string>& missing) {
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

fs::path resolveModelDir(const Config& cfg) {
    return cfg.modelDir;
}

bool canCreateDirectory(const fs::path& path) {
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

std::vector<RuntimeCheck> runtimeChecks(const Config& cfg) {
    const fs::path modelDir = resolveModelDir(cfg);
    const ModelEntry* entry = findModel(cfg.modelName);

    std::string modelDetail;
    bool modelOk = false;
    if (modelDir.empty() || entry == nullptr) {
        const char* display = entry ? entry->displayName.c_str() : "Qwen3-ASR-0.6B";
        modelDetail = std::string("未下载 — 打开 EchoFlow 设置 → 模型 下载 ") + display;
    } else if (!fs::exists(modelDir)) {
        modelDetail = modelDir.string();
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

int runSelfTest(const Config& cfg) {
    bool ok = true;
    for (const auto& check : runtimeChecks(cfg)) {
        std::printf("[%s] %s: %s\n", check.passed ? "OK" : "FAIL",
                    check.name.c_str(), check.detail.c_str());
        ok = ok && check.passed;
    }
    return ok ? 0 : 1;
}

}  // namespace echoflow
```

- [ ] **Step 5: Run, expect PASS**

```bash
cmake --build build && ctest --test-dir build -R "test_selftest|test_config|test_model_catalog" --output-on-failure
```

Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add service/SelfTest.h service/SelfTest.cpp tests/test_selftest.cpp
git commit -m "SelfTest uses ModelCatalog; actionable message when model missing"
```

---

## Task 4: service `--print-default-config`

**Files:**
- Modify: `service/main.cpp`

- [ ] **Step 1: Replace `printDefaultConfig()`**

In `service/main.cpp`, replace the `printDefaultConfig()` function body with:

```cpp
void printDefaultConfig()
{
    auto cfg = echoflow::Config::defaultConfig();
    std::printf("{\n"
                "  \"model_name\": \"%s\",\n"
                "  \"model_dir\": \"(derived at runtime: <config_dir>/<model_name>)\",\n"
                "  \"language\": \"%s\",\n"
                "  \"prompt\": \"%s\",\n"
                "  \"recordings_dir\": \"%s\",\n"
                "  \"min_record_seconds\": %.2f,\n"
                "  \"rate\": %d,\n"
                "  \"channels\": %d,\n"
                "  \"format\": \"%s\",\n"
                "  \"fcitx_commit\": %s\n"
                "}\n",
                cfg.modelName.c_str(),
                cfg.language.value_or("").c_str(), cfg.prompt.c_str(),
                cfg.recordingsDir.c_str(), cfg.minRecordSeconds,
                cfg.pwRecord.rate, cfg.pwRecord.channels, cfg.pwRecord.format.c_str(),
                cfg.fcitxCommit ? "true" : "false");
}
```

- [ ] **Step 2: Build and sanity-run**

```bash
cmake --build build && ./build/service/echoflow-service --print-default-config
```

Expected: JSON with `"model_name": "qwen3-asr-0.6b"` and `"model_dir": "(derived at runtime: ...)"`, no concrete path.

- [ ] **Step 3: Commit**

```bash
git add service/main.cpp
git commit -m "print-default-config notes derived model_dir"
```

---

## Task 5: settings-schema.json + EchoFlowSettings defaults

**Files:**
- Modify: `ui-host/settings-schema.json`
- Modify: `ui-host/EchoFlowSettings.cpp`

- [ ] **Step 1: Rewrite the schema's `basic.model` group and drop `model_dir`**

In `ui-host/settings-schema.json`, replace the entire `basic.model` group object with:

```json
        {
          "key": "model",
          "name": "模型",
          "options": [
            {
              "key": "model_name",
              "name": "模型名称",
              "type": "combobox",
              "default": "qwen3-asr-0.6b"
            },
            {
              "key": "download_0.6b",
              "name": "Qwen3-ASR-0.6B",
              "type": "modeldownload",
              "default": ""
            },
            {
              "key": "download_1.7b",
              "name": "Qwen3-ASR-1.7B",
              "type": "modeldownload",
              "default": ""
            },
            {
              "key": "mirror",
              "name": "下载源",
              "type": "combobox",
              "default": "hf-mirror"
            }
          ]
        }
```

Then, in the `advanced.runtime` group, **delete** the `model_dir` option object (the one with `"key": "model_dir"`), leaving only `asr_timeout_seconds`.

- [ ] **Step 2: Update combobox items + default-write list**

In `ui-host/EchoFlowSettings.cpp`:

(a) Replace the `model_name` line in `populateComboBoxes()`:
```cpp
    setComboBoxItems(dsettings_, QStringLiteral("basic.model.model_name"),
                     QStringList{QStringLiteral("qwen3-asr-0.6b"),
                                 QStringLiteral("qwen3-asr-1.7b")});
```

(b) Add `mirror` items immediately after it:
```cpp
    setComboBoxItems(dsettings_, QStringLiteral("basic.model.mirror"),
                     QStringList{QStringLiteral("hf-mirror"),
                                 QStringLiteral("official")});
```

(c) In the default-write `paths` list, replace the line `"advanced.runtime.model_dir",` with `"basic.model.mirror",`. Do **not** add the `download_0.6b`/`download_1.7b` keys to that list (they are action widgets with no persisted value).

- [ ] **Step 3: Build the UI host**

```bash
cmake --build build --target echoflow-ui
```

Expected: builds (the `modeldownload` type is unknown to DTK until Task 9 registers it, but unknown types don't fail the build — DTK renders a fallback widget; Task 9 replaces it).

- [ ] **Step 4: Commit**

```bash
git add ui-host/settings-schema.json ui-host/EchoFlowSettings.cpp
git commit -m "Schema: two model rows + mirror; drop model_dir"
```

---

## Task 6: install-user.sh default config

**Files:**
- Modify: `install-user.sh`

- [ ] **Step 1: Rewrite the default config block**

In `install-user.sh`, replace the `cat > "$CONFIG_DIR/echoflow.conf" <<EOF ... EOF` block with:

```bash
  cat > "$CONFIG_DIR/echoflow.conf" <<EOF
[basic.model.model_name]
value=qwen3-asr-0.6b
[basic.model.mirror]
value=hf-mirror
[basic.recognition.language]
value=Chinese
[basic.recognition.prompt]
value=
[basic.recognition.strip_trailing_punctuation]
value=false
[basic.recording.min_record_seconds]
value=0.25
[basic.recording.rate]
value=16000
[basic.recording.channels]
value=1
[basic.recording.format]
value=s16
[advanced.runtime.asr_timeout_seconds]
value=120
[advanced.fcitx.fcitx_commit]
value=true
[advanced.storage.recordings_dir]
value=\$HOME/.local/share/echoflow/recordings
EOF
```

(Removes `[advanced.runtime.model_dir]`; adds `[basic.model.mirror]`; sets `model_name=qwen3-asr-0.6b`.)

- [ ] **Step 2: Syntax check**

```bash
bash -n install-user.sh
```

Expected: no output (success).

- [ ] **Step 3: Commit**

```bash
git add install-user.sh
git commit -m "install-user.sh: default config drops model_dir, adds mirror"
```

---

## Task 7: ModelDownloader (ui-host)

**Files:**
- Create: `ui-host/ModelDownloader.h`
- Create: `ui-host/ModelDownloader.cpp`
- Modify: `ui-host/CMakeLists.txt`

- [ ] **Step 1: Create the header**

Create `ui-host/ModelDownloader.h`:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_MODEL_DOWNLOADER_H
#define ECHOFLOW_MODEL_DOWNLOADER_H

#include "ModelCatalog.h"

#include <QObject>
#include <QString>

namespace echoflow {

// Downloads every missing file of one ModelEntry into targetDir via Qt6 Network.
// Files already present at their final name are skipped. Each in-flight file is
// written to <dir>/.<file>.part and renamed on completion; stale .part files
// from a previous run are removed before fetching. Sequential.
class ModelDownloader : public QObject {
    Q_OBJECT
public:
    ModelDownloader(const ModelEntry& entry,
                    const QString& targetDir,
                    const QString& baseUrl,
                    QObject* parent = nullptr);
    ~ModelDownloader() override;

    void start();
    void cancel();

signals:
    void progress(qint64 done, qint64 total, const QString& currentFile);
    void finished(bool ok, const QString& error);

private:
    void fetchNext();
    QString urlFor(const std::string& file) const;

    ModelEntry entry_;
    QString targetDir_;
    QString baseUrl_;
    class QNetworkAccessManager* nam_ = nullptr;
    class QNetworkReply* reply_ = nullptr;

    std::vector<std::string> pending_;
    size_t currentIndex_ = 0;
    QString currentFile_;
    qint64 completedBytes_ = 0;    // sum of fully-downloaded files
    qint64 currentReceived_ = 0;   // bytes received for the current file
    qint64 bytesTotalKnown_ = 0;   // sum of Content-Length across files
    qint64 bytesTotalUnknown_ = 0; // set to 1 if any response lacked length
    bool currentFileCounted_ = false;
};

}  // namespace echoflow

#endif  // ECHOFLOW_MODEL_DOWNLOADER_H
```

- [ ] **Step 2: Create the implementation**

Create `ui-host/ModelDownloader.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ModelDownloader.h"

#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace echoflow {

ModelDownloader::ModelDownloader(const ModelEntry& entry,
                                 const QString& targetDir,
                                 const QString& baseUrl,
                                 QObject* parent)
    : QObject(parent), entry_(entry), targetDir_(targetDir), baseUrl_(baseUrl)
{
    nam_ = new QNetworkAccessManager(this);
}

ModelDownloader::~ModelDownloader() {
    if (reply_) {
        reply_->abort();
    }
}

QString ModelDownloader::urlFor(const std::string& file) const {
    // {base}/{repo}/resolve/main/{file}
    return baseUrl_ + QStringLiteral("/") +
           QString::fromStdString(entry_.repo) +
           QStringLiteral("/resolve/main/") +
           QString::fromStdString(file);
}

void ModelDownloader::start() {
    QDir().mkpath(targetDir_);
    for (const auto& file : entry_.files) {
        const QString finalPath = targetDir_ + QStringLiteral("/") + QString::fromStdString(file);
        if (!QFile::exists(finalPath)) {
            pending_.push_back(file);
        }
    }
    if (pending_.empty()) {
        emit finished(true, QString());
        return;
    }
    currentIndex_ = 0;
    completedBytes_ = 0;
    currentReceived_ = 0;
    bytesTotalKnown_ = 0;
    bytesTotalUnknown_ = 0;
    fetchNext();
}

void ModelDownloader::cancel() {
    if (reply_) {
        reply_->abort();
    }
    if (!currentFile_.isEmpty()) {
        QFile::remove(targetDir_ + QStringLiteral("/.") + currentFile_ + QStringLiteral(".part"));
    }
    emit finished(false, QStringLiteral("已取消"));
}

void ModelDownloader::fetchNext() {
    if (currentIndex_ >= pending_.size()) {
        emit finished(true, QString());
        return;
    }
    currentFile_ = QString::fromStdString(pending_[currentIndex_]);
    currentReceived_ = 0;
    currentFileCounted_ = false;

    const QString partPath = targetDir_ + QStringLiteral("/.") + currentFile_ + QStringLiteral(".part");
    QFile::remove(partPath);  // stale fragment from a crashed run

    QNetworkRequest req(urlFor(pending_[currentIndex_]));
    req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("echoflow"));
    reply_ = nam_->get(req);

    // readyRead only appends bytes to the .part file; byte accounting lives in
    // downloadProgress to avoid drift between the two signals.
    QObject::connect(reply_, &QNetworkReply::readyRead, this, [this, partPath]() {
        QFile f(partPath);
        if (f.open(QIODevice::Append)) {
            f.write(reply_->readAll());
        }
    });

    // Count each file's total exactly once, summed across all files.
    auto countTotal = [this](qint64 total) {
        if (currentFileCounted_) {
            return;
        }
        if (total > 0) {
            bytesTotalKnown_ += total;
            currentFileCounted_ = true;
        } else {
            bytesTotalUnknown_ = 1;
        }
    };

    QObject::connect(reply_, &QNetworkReply::metaDataChanged, this, [this, countTotal]() {
        bool ok = false;
        const qint64 fileTotal = reply_->header(QNetworkRequest::ContentLengthHeader).toLongLong(&ok);
        countTotal(ok ? fileTotal : 0);
    });

    QObject::connect(reply_, &QNetworkReply::downloadProgress, this,
        [this, countTotal](qint64 received, qint64 total) {
            countTotal(total);
            currentReceived_ = received;
            const qint64 done = completedBytes_ + currentReceived_;
            const qint64 effectiveTotal = bytesTotalUnknown_ ? 0 : bytesTotalKnown_;
            emit progress(done, effectiveTotal, currentFile_);
        });

    QObject::connect(reply_, &QNetworkReply::finished, this, [this, partPath]() {
        QNetworkReply::NetworkError err = reply_->error();
        reply_->deleteLater();
        reply_ = nullptr;

        if (err != QNetworkReply::NoError) {
            QFile::remove(partPath);
            emit finished(false, QStringLiteral("网络错误: ") + QString::number(err));
            return;
        }
        const QString finalPath = targetDir_ + QStringLiteral("/") + currentFile_;
        if (!QFile::rename(partPath, finalPath)) {
            QFile::remove(finalPath);
            if (!QFile::rename(partPath, finalPath)) {
                QFile::remove(partPath);
                emit finished(false, QStringLiteral("无法写入: ") + finalPath);
                return;
            }
        }
        completedBytes_ += currentReceived_;
        currentReceived_ = 0;
        ++currentIndex_;
        fetchNext();
    });
}

}  // namespace echoflow
```

Note: `bytesTotalKnown_` accumulates each file's Content-Length exactly once (via `countTotal`, called from both `metaDataChanged` and `downloadProgress`), so aggregate percent = `(completedBytes_ + currentReceived_) / bytesTotalKnown_` stays stable across the multi-file 1.7B download. When any response lacks Content-Length, `bytesTotalUnknown_` is set and the row switches to indeterminate mode (`total == 0`).

- [ ] **Step 3: Wire CMake (Qt6::Network, new files, service include)**

In `ui-host/CMakeLists.txt`:

(a) Add `Network` to `find_package`:
```cmake
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Widgets Network)
```

(b) Add the new sources to the executable:
```cmake
add_executable(echoflow-ui
    main.cpp
    EchoFlowSettings.cpp
    SettingsDialog.cpp
    ModelDownloader.cpp
    ModelRowWidget.cpp
    settings.qrc
    qml.qrc
)
```
(`ModelRowWidget.cpp` is added here in advance; it's created in Task 8 — keep it in the list; CMake will fail until Task 8 lands. To keep this task independently buildable, **do not** add `ModelRowWidget.cpp` yet; add only `ModelDownloader.cpp` here, and add `ModelRowWidget.cpp` in Task 8.)

So in this task the executable block is:
```cmake
add_executable(echoflow-ui
    main.cpp
    EchoFlowSettings.cpp
    SettingsDialog.cpp
    ModelDownloader.cpp
    settings.qrc
    qml.qrc
)
```

(c) Add the include dir for `service/ModelCatalog.h` and link `Qt6::Network`:
```cmake
target_include_directories(echoflow-ui PRIVATE ${CMAKE_SOURCE_DIR}/service)

target_link_libraries(echoflow-ui
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::Widgets
        Qt6::Network
        Dtk6::Widget
        Dtk6::Core
        Dtk6::Gui
)
```

- [ ] **Step 4: Build**

```bash
cmake --build build --target echoflow-ui
```

Expected: builds (ModelDownloader is compiled but not yet used by main.cpp; that's fine — it's a self-contained translation unit. If the compiler warns about an unused class, ignore; Task 8/9 wire it in.)

- [ ] **Step 5: Commit**

```bash
git add ui-host/ModelDownloader.h ui-host/ModelDownloader.cpp ui-host/CMakeLists.txt
git commit -m "Add ModelDownloader (Qt6 Network, .part + progress)"
```

---

## Task 8: ModelRowWidget (ui-host)

**Files:**
- Create: `ui-host/ModelRowWidget.h`
- Create: `ui-host/ModelRowWidget.cpp`
- Modify: `ui-host/CMakeLists.txt` (add `ModelRowWidget.cpp`)

- [ ] **Step 1: Create the header**

Create `ui-host/ModelRowWidget.h`:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_MODEL_ROW_WIDGET_H
#define ECHOFLOW_MODEL_ROW_WIDGET_H

#include "ModelCatalog.h"

#include <QWidget>

class QLabel;
class QPushButton;

namespace echoflow {

class ModelDownloader;

class ModelRowWidget : public QWidget {
    Q_OBJECT
public:
    explicit ModelRowWidget(const ModelEntry* entry, QWidget* parent = nullptr);

private slots:
    void onClicked();
    void onProgress(qint64 done, qint64 total, const QString& currentFile);
    void onFinished(bool ok, const QString& error);

private:
    void refreshState();

    const ModelEntry* entry_;
    QLabel* status_ = nullptr;
    QPushButton* button_ = nullptr;
    ModelDownloader* downloader_ = nullptr;
};

}  // namespace echoflow

#endif  // ECHOFLOW_MODEL_ROW_WIDGET_H
```

- [ ] **Step 2: Create the implementation**

Create `ui-host/ModelRowWidget.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ModelRowWidget.h"

#include "EchoFlowSettings.h"
#include "ModelDownloader.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

#include <DSettings>
#include <DSettingsOption>

namespace echoflow {

namespace {

// Config dir = parent of echoflow.conf.
QString configDir() {
    const QString conf = EchoFlowSettings::instance()->configPath();
    const int slash = conf.lastIndexOf(QLatin1Char('/'));
    return slash > 0 ? conf.left(slash) : QStringLiteral(".");
}

// Read the mirror setting live (so changing the combobox takes effect).
QString baseUrlFromMirror() {
    auto* ds = EchoFlowSettings::instance()->dsettings();
    QString mirror = QStringLiteral("hf-mirror");
    if (ds) {
        auto* opt = ds->option(QStringLiteral("basic.model.mirror"));
        if (opt) {
            const QVariant v = opt->value();
            if (v.isValid() && !v.toString().isEmpty()) {
                mirror = v.toString();
            }
        }
    }
    return mirror == QLatin1String("official")
               ? QStringLiteral("https://huggingface.co")
               : QStringLiteral("https://hf-mirror.com");
}

}  // namespace

ModelRowWidget::ModelRowWidget(const ModelEntry* entry, QWidget* parent)
    : QWidget(parent), entry_(entry)
{
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    status_ = new QLabel(this);
    button_ = new QPushButton(this);
    lay->addStretch(1);
    lay->addWidget(status_);
    lay->addWidget(button_);

    connect(button_, &QPushButton::clicked, this, &ModelRowWidget::onClicked);
    refreshState();
}

void ModelRowWidget::refreshState() {
    if (!entry_) {
        status_->setText(QString());
        button_->setText(QStringLiteral("未知"));
        button_->setEnabled(false);
        return;
    }
    if (downloader_) {
        return;  // downloading; state driven by onProgress/onFinished
    }
    const QString dir = configDir() + QStringLiteral("/") +
                        QString::fromStdString(entry_->id);
    const bool present = isModelPresent(std::filesystem::path(dir.toStdString()), *entry_);
    if (present) {
        status_->setText(QString());
        button_->setText(QStringLiteral("已下载"));
        button_->setEnabled(false);
    } else {
        status_->setText(QString());
        button_->setText(QStringLiteral("下载"));
        button_->setEnabled(true);
    }
}

void ModelRowWidget::onClicked() {
    if (!entry_ || downloader_) {
        return;
    }
    const QString dir = configDir() + QStringLiteral("/") +
                        QString::fromStdString(entry_->id);
    downloader_ = new ModelDownloader(*entry_, dir, baseUrlFromMirror(), this);
    connect(downloader_, &ModelDownloader::progress, this, &ModelRowWidget::onProgress);
    connect(downloader_, &ModelDownloader::finished, this, &ModelRowWidget::onFinished);
    button_->setText(QStringLiteral("取消"));
    button_->setEnabled(true);
    downloader_->start();
}

void ModelRowWidget::onProgress(qint64 done, qint64 total, const QString& /*currentFile*/) {
    if (total > 0) {
        const int pct = static_cast<int>((done * 100) / total);
        status_->setText(QString::number(qBound(0, pct, 100)) + QStringLiteral("%"));
    } else {
        // Indeterminate: show downloaded megabytes.
        const double mb = done / (1024.0 * 1024.0);
        status_->setText(QString("%1 MB").arg(mb, 0, 'f', 1));
        button_->setText(QStringLiteral("下载中"));
        button_->setEnabled(false);
    }
}

void ModelRowWidget::onFinished(bool ok, const QString& error) {
    downloader_->deleteLater();
    downloader_ = nullptr;
    if (!ok && !error.isEmpty()) {
        status_->setText(error);
        button_->setText(QStringLiteral("重试"));
        button_->setEnabled(true);
        return;
    }
    refreshState();
}

}  // namespace echoflow
```

- [ ] **Step 3: Add the source to CMake**

In `ui-host/CMakeLists.txt`, add `ModelRowWidget.cpp` to the `add_executable` list:

```cmake
add_executable(echoflow-ui
    main.cpp
    EchoFlowSettings.cpp
    SettingsDialog.cpp
    ModelDownloader.cpp
    ModelRowWidget.cpp
    settings.qrc
    qml.qrc
)
```

- [ ] **Step 4: Build**

```bash
cmake --build build --target echoflow-ui
```

Expected: builds. (`configDir()` reads `EchoFlowSettings::configPath()`; the mirror is read from the live `DSettings`.)

- [ ] **Step 5: Commit**

```bash
git add ui-host/ModelRowWidget.h ui-host/ModelRowWidget.cpp ui-host/CMakeLists.txt
git commit -m "Add ModelRowWidget (status label + download/cancel/已下载 button)"
```

---

## Task 9: Register the `modeldownload` DTK widget factory

**Files:**
- Modify: `ui-host/SettingsDialog.cpp`
- Modify: `ui-host/SettingsDialog.h`

- [ ] **Step 1: Register the factory in the settings dialog**

In `ui-host/SettingsDialog.cpp`, replace the file with:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SettingsDialog.h"

#include "ModelCatalog.h"
#include "ModelRowWidget.h"

#include <DSettings>
#include <DSettingsOption>
#include <DSettingsWidgetFactory>

namespace echoflow {

namespace {

const ModelEntry* entryForOption(Dtk::Core::DSettingsOption* opt) {
    if (!opt) {
        return nullptr;
    }
    // DTK6 hands the factory a QObject*; map by option key (reliable across
    // DTK versions — schema JSON -> option data wiring is not guaranteed).
    const QString key = opt->key();
    if (key.endsWith(QStringLiteral("download_0.6b"))) {
        return findModel("qwen3-asr-0.6b");
    }
    if (key.endsWith(QStringLiteral("download_1.7b"))) {
        return findModel("qwen3-asr-1.7b");
    }
    return nullptr;
}

}  // namespace

SettingsDialog::SettingsDialog(Dtk::Core::DSettings *settings, QWidget *parent)
    : Dtk::Widget::DSettingsDialog(parent) {
    setWindowTitle(tr("EchoFlow 设置"));

    // DSettingsDialog owns its factory (no global singleton). Register the
    // custom type on it BEFORE updateSettings builds the rows.
    auto* factory = widgetFactory();
    factory->registerWidget(QStringLiteral("modeldownload"),
        [](QObject* obj) -> QWidget* {
            auto* opt = qobject_cast<Dtk::Core::DSettingsOption*>(obj);
            return new ModelRowWidget(entryForOption(opt));
        });

    updateSettings(settings);
}

}  // namespace echoflow
```

- [ ] **Step 2: Confirm SettingsDialog.h has the needed include**

`ui-host/SettingsDialog.h` already includes `<DSettings>` and forward-declares `DSettings`. No change required. If the build complains about `qobject_cast` needing the full type, ensure `#include <DSettingsOption>` is present (it is, via the .cpp include added above).

- [ ] **Step 3: Build the UI host**

```bash
cmake --build build --target echoflow-ui
```

Expected: builds.

- [ ] **Step 4: Manual smoke test (optional, needs a display)**

```bash
./build/ui-host/echoflow-ui &
# Right-click the tray icon → 设置; confirm two rows render under 模型,
# each showing 下载 (or 已下载 if a model is already present). Close.
```

If headless, rely on the build + spec test in Task 11.

- [ ] **Step 5: Commit**

```bash
git add ui-host/SettingsDialog.cpp
git commit -m "Register modeldownload DTK widget factory"
```

---

## Task 10: Remove the download script

**Files:**
- Delete: `scripts/setup-qwen-asr-0.6b.sh`
- Delete: `scripts/` (if empty after the above)

- [ ] **Step 1: Remove the script and its directory**

```bash
git rm scripts/setup-qwen-asr-0.6b.sh
rmdir scripts 2>/dev/null || true
```

- [ ] **Step 2: Confirm nothing else references the script**

```bash
rg -n "setup-qwen-asr" --glob '!build/**' --glob '!.git/**' --glob '!docs/superpowers/**' || echo "no references"
```

Expected: `no references` (the spec/plan docs under `docs/superpowers/` are intentionally excluded; `README.md`/`AGENTS.md` are updated in Task 12).

- [ ] **Step 3: Commit**

```bash
git add -A scripts
git commit -m "Remove setup-qwen-asr-0.6b.sh; download now lives in-app"
```

---

## Task 11: Update the bash spec test

**Files:**
- Modify: `tests/spec/run_spec.sh`

- [ ] **Step 1: Replace the script assertions and add schema/install checks**

In `tests/spec/run_spec.sh`, replace these three lines:
```bash
assert_absent "$ROOT/scripts/setup-qwen-asr-0.6b.sh" "/home/hualet/projects/hualet/echoflow/model" "setup script has no repo-local model path"
assert_contains "$ROOT/scripts/setup-qwen-asr-0.6b.sh" "third_party/qwen-asr" "setup script uses qwen-asr submodule"
assert_contains "$ROOT/scripts/setup-qwen-asr-0.6b.sh" "--model small" "setup script downloads small safetensors model"
```
with:
```bash
assert_script_absent() {
  if [[ ! -e "$ROOT/scripts/setup-qwen-asr-0.6b.sh" ]]; then
    echo "ok   - setup script removed"
    pass=$((pass + 1))
  else
    echo "FAIL - setup script should be removed ($ROOT/scripts/setup-qwen-asr-0.6b.sh still exists)"
    fail=$((fail + 1))
  fi
}
assert_script_absent

assert_contains "$ROOT/ui-host/settings-schema.json" "modeldownload" "settings schema has modeldownload widget type"
assert_contains "$ROOT/ui-host/settings-schema.json" "Qwen3-ASR-0.6B" "settings schema lists 0.6B model row"
assert_contains "$ROOT/ui-host/settings-schema.json" "Qwen3-ASR-1.7B" "settings schema lists 1.7B model row"
assert_contains "$ROOT/ui-host/settings-schema.json" "hf-mirror" "settings schema has hf-mirror download source"
assert_absent "$ROOT/ui-host/settings-schema.json" "model_dir" "settings schema has no model_dir option"
assert_absent "$ROOT/install-user.sh" "model_dir" "install-user.sh writes no model_dir"
assert_contains "$ROOT/ui-host/SettingsDialog.cpp" "modeldownload" "SettingsDialog registers modeldownload factory"
assert_contains "$ROOT/service/ModelCatalog.h" "Qwen/Qwen3-ASR-1.7B" "ModelCatalog knows the 1.7B repo"
```

- [ ] **Step 2: Syntax-check and run**

```bash
bash -n tests/spec/run_spec.sh && ctest --test-dir build -R spec_tests --output-on-failure
```

Expected: all spec assertions `ok`, `spec: N passed, 0 failed`.

- [ ] **Step 3: Commit**

```bash
git add tests/spec/run_spec.sh
git commit -m "spec: assert in-app model download wiring, script removed"
```

---

## Task 12: Documentation

**Files:**
- Modify: `README.md`
- Modify: `AGENTS.md`

- [ ] **Step 1: README — replace the "准备模型" section**

In `README.md`, replace the section currently spanning the "准备模型" heading through the `jfk.wav` verification block with:

```markdown
## 准备模型

模型权重不放进仓库。打开 EchoFlow 托盘菜单 → **设置** → **模型**，点击对应模型右侧的 **下载** 按钮即可：

- **Qwen3-ASR-0.6B**：默认模型，体积小、速度快。
- **Qwen3-ASR-1.7B**：精度更高，体积更大。

下载进度会实时显示在按钮左侧；下载完成后按钮变为 **已下载**。默认下载源为 `hf-mirror`（国内可达），可在「下载源」切换为 `official`（huggingface.co）。

模型下载到与配置文件相同的目录：

```text
~/.config/echoflow/qwen3-asr-0.6b
~/.config/echoflow/qwen3-asr-1.7b
```

下载完成后可用已有 wav 验证：

```bash
./build/service/echoflow-service --transcribe-file third_party/qwen-asr/samples/jfk.wav
```
```

Also update the 配置 section: remove the `advanced.runtime.model_dir` bullet; add:

```markdown
- `basic.model.model_name`: 活动模型，`qwen3-asr-0.6b` 或 `qwen3-asr-1.7b`（模型目录由该值推导）。
- `basic.model.mirror`: 下载源，`hf-mirror`（默认）或 `official`。
```

- [ ] **Step 2: AGENTS.md — drop the script command and update notes**

In `AGENTS.md`:

(a) In the commands block, remove:
```bash
# Prepare the default qwen-asr 0.6B safetensors model
./scripts/setup-qwen-asr-0.6b.sh
```

(b) Replace the "Conventions & gotchas" bullets about model paths with:

```text
- Models are downloaded from the settings dialog (DTK `modeldownload` rows) into
  the config directory (`~/.config/echoflow/qwen3-asr-0.6b` / `...1.7b`). There
  is no download script.
- `service/ModelCatalog.h` is the single source of truth for model id / display
  name / HF repo / file list; both `ui-host` (download) and `service/SelfTest`
  (verification) include it. Do not duplicate the file lists elsewhere.
- Model download lives in `ui-host` (Qt6 Network). `echoflow-service` never
  performs HTTP.
- `Config::modelDir` is derived from `model_name` + the config-file directory;
  the `model_dir` config key no longer exists.
```

(c) If the "Architecture — boundaries" section mentions model setup, leave the
qwen-asr boundary note as-is; it is unaffected.

- [ ] **Step 3: Build + full test**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: all tests pass (ctest includes the bash spec, which now also reads README implicitly? No — the spec does not read README. Docs changes are not asserted by ctest; only build success matters here.)

- [ ] **Step 4: Commit**

```bash
git add README.md AGENTS.md
git commit -m "Docs: in-app model download replaces setup script"
```

---

## Task 13: Full verification

- [ ] **Step 1: Clean configure + build + all tests**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: every test passes, including `test_model_catalog`, `test_config`, `test_selftest`, and `spec_tests`.

- [ ] **Step 2: CLI sanity checks**

```bash
./build/service/echoflow-service --print-default-config
./build/service/echoflow-service --self-test
bash -n install-user.sh uninstall-user.sh tests/spec/run_spec.sh
sh -n run.sh
```

Expected: `--print-default-config` shows `model_name: qwen3-asr-0.6b` and the derived-path note; `--self-test` shows `[FAIL] model available: 未下载 ...` on a machine without the model (or `[OK]` if present); all syntax checks silent.

- [ ] **Step 3: Confirm no stale references repo-wide**

```bash
# The download script should be referenced nowhere outside the spec/plan docs.
rg -n "setup-qwen-asr" --glob '!build/**' --glob '!.git/**' --glob '!docs/superpowers/**' || echo "script: clean"
# The model_dir config key must not appear in product code (it is intentionally
# referenced by tests/test_config.cpp::loadDtkConfIgnoresModelDirKey to prove it
# is ignored, so tests/ are excluded).
rg -n "advanced.runtime.model_dir" --glob '!build/**' --glob '!.git/**' --glob '!docs/superpowers/**' --glob '!tests/**' || echo "model_dir key: clean"
```

Expected: `script: clean` and `model_dir key: clean`.

- [ ] **Step 4: Final commit (if any verification fixups were needed)**

Only commit if Step 1–3 surfaced changes; otherwise this task is verification-only and produces no commit.

---

## Self-review checklist (already run, kept for reference)

- **Spec coverage**: catalog (T1), Config derivation + normalize + drop model_dir (T2), SelfTest catalog + actionable msg (T3), print-default-config (T4), schema + settings defaults (T5), install-user.sh (T6), downloader (T7), row widget (T8), factory (T9), script removal (T10), spec test (T11), docs (T12), verify (T13). Every spec section maps to a task.
- **Type consistency**: `ModelEntry`/`findModel`/`missingModelFiles(dir, entry)`/`isModelPresent` defined in T1, used identically in T3 and T8. `normalizeModelName(value)` defined T2, declared in Config.h. `resolveModelDir(cfg)` trivial in T3.
- **Placeholders**: none; every code step shows the full code.
