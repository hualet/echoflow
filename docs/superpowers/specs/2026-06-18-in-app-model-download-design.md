# In-app model download — Design

- Date: 2026-06-18
- Status: Approved (brainstorm)
- Scope: Replace the `scripts/setup-qwen-asr-0.6b.sh` download script with
  in-program model download, surfaced as two rows in the existing DTK settings
  dialog (Qwen3-ASR 0.6B and 1.7B), each with a download button and progress,
  downloading into the EchoFlow config directory.

## Goal / user stories

- As a user, I can open EchoFlow settings and download the 0.6B or 1.7B
  Qwen3-ASR safetensors model with one click, seeing progress while it
  downloads.
- A model that is already present shows "已下载" and its button is disabled.
- Downloaded models land next to `echoflow.conf` (i.e. under the EchoFlow
  config directory), not under `$HOME/AI/Model`.
- The standalone download script is gone; the program owns the capability.

## Non-goals (YAGNI)

- No automatic migration of pre-existing `$HOME/AI/Model/qwen3-asr-*` models.
  Users re-download with one click; documented in README.
- No resuming of `.part` fragments beyond re-downloading the file.
- No checksum verification; presence + non-zero size only.
- No per-file progress breakdown in the UI; aggregate percent + current file
  name is enough.
- Switching the active model still requires restarting `echoflow-service`
  (unchanged behavior — config is read once at startup).

## Decisions locked in brainstorm

1. **UI placement**: DTK custom widget rows inside the existing settings
   dialog. Two options of a new `modeldownload` type; each option's `name`
   renders on the left (model display name), the custom widget renders on the
   right (status/progress + button).
2. **Download source**: default `https://hf-mirror.com` (reachable from China),
  with a settings combobox to switch to the official `https://huggingface.co`.
3. **Model path**: remove the free-text `advanced.runtime.model_dir` option;
   derive the path from `model_name` as `<config_dir>/<model_name>`.

## Architecture & boundaries

- **Download lives in `ui-host`.** It is HTTP I/O plus progress UI, which Qt6
  Network and the UI host already own. `echoflow-service` gains no HTTP client
  and no new socket protocol; it continues to load whatever path is in
  `Config::modelDir`. This respects the existing boundary that only
  `service/AsrEngine.cpp` includes `qwen_asr.h` (download never touches the
  qwen-asr C API).
- **Model catalog is one header-only source of truth.** A new
  `service/ModelCatalog.h` lists each model's id, display name, HuggingFace
  repo id, and expected file list, plus `missingModelFiles(dir, entry)` (the
  detailed list) and `isModelPresent(dir, entry)` helpers that treat the 1.7B
  two-shard layout as satisfying `model.safetensors`. Both `ui-host` (what to
  fetch) and `service/SelfTest.cpp` (what to verify) include it. This replaces
  SelfTest's hardcoded `kRequiredModelFiles` and large-shard logic and removes
  drift between "files we download" and "files we check".
- **Model path is derived, not configured.** `Config::modelDir` is populated
  from `modelName` plus the config file's parent directory; the `model_dir`
  config key is no longer read.

## Components

### `service/ModelCatalog.h` (new, header-only)

No dependencies beyond `<filesystem>`, `<string>`, `<vector>`. Contents:

```cpp
namespace echoflow {

struct ModelEntry {
    std::string id;          // "qwen3-asr-0.6b"  (also the directory name)
    std::string displayName; // "Qwen3-ASR-0.6B"
    std::string repo;        // "Qwen/Qwen3-ASR-0.6B"
    std::vector<std::string> files;
};

// Ordered: 0.6B first, then 1.7B.
const std::vector<ModelEntry>& modelCatalog();

// Find by id. Returns nullptr if unknown.
const ModelEntry* findModel(const std::string& id);

// Required files under dir that are not present, where a 1.7B shard pair
// (model-00001-of-00002.safetensors + model-00002-of-00002.safetensors)
// satisfies the single-shard requirement. Empty vector == fully present.
std::vector<std::string> missingModelFiles(
    const std::filesystem::path& dir, const ModelEntry& e);

// Convenience: missingModelFiles(...).empty().
bool isModelPresent(const std::filesystem::path& dir, const ModelEntry& e);

}  // namespace echoflow
```

Catalog data (mirrors `third_party/qwen-asr/download_model.sh`):

- `qwen3-asr-0.6b` / `Qwen3-ASR-0.6B` / `Qwen/Qwen3-ASR-0.6B`:
  `config.json`, `generation_config.json`, `model.safetensors`, `vocab.json`,
  `merges.txt`.
- `qwen3-asr-1.7b` / `Qwen3-ASR-1.7B` / `Qwen/Qwen3-ASR-1.7B`:
  `config.json`, `generation_config.json`, `model.safetensors.index.json`,
  `model-00001-of-00002.safetensors`, `model-00002-of-00002.safetensors`,
  `vocab.json`, `merges.txt`.

### `ui-host/ModelDownloader.{h,cpp}` (new)

`QObject` wrapping a `QNetworkAccessManager`. Per download:

- Inputs: `ModelEntry`, target directory, base URL
  (`https://hf-mirror.com` or `https://huggingface.co`).
- URL template per file: `{base}/{repo}/resolve/main/{file}` — mirroring
  `download_model.sh`'s `BASE_URL = https://huggingface.co/${MODEL_ID}/resolve/main`
  with `{base}` swapped in. The implementer must not use `{base}/{repo}/{file}`.
- For each file in `entry.files`: if already present at the final name, skip;
  otherwise download to `<dir>/.<file>.part`, then `rename` on completion. On
  error or cancel, remove the `.part`.
- Progress total: sum Content-Length across the files to be fetched (taken from
  each GET response — no separate HEAD round-trip). Percent = cumulative bytes
  / total. If a response omits Content-Length (total unknown), switch the row
  to **indeterminate mode**: show downloaded bytes (e.g. "12.3 MB") with button
  text "下载中", never a misleading 0%.
- Before fetching a file, remove any stale `.<file>.part` left by a previous
  crash. It never matches the final filename (so it can't fake "present"), but
  clearing it avoids truncate/append confusion. Resuming a `.part` is an
  explicit non-goal.
- Signals: `progress(qint64 done, qint64 total, const QString& currentFile)`,
  `finished(bool ok, const QString& error)`.
- Slot: `cancel()` — aborts the in-flight reply and cleans the `.part`.
- Sequential downloads (one file at a time) to keep progress math simple and
  avoid hammering the mirror.

### `ui-host/ModelRowWidget` (new `QWidget`)

Horizontal layout: `[ stretch | statusLabel | button ]`. The model display name
is already rendered by DTK as the option `name` on the left, so this widget is
only the right-hand contents. Constructed from a `DSettingsOption*` (its key
identifies the model id). States:

| State          | statusLabel      | button text   | button enabled |
|----------------|------------------|---------------|----------------|
| Missing        | (empty)          | 下载          | yes            |
| Downloading    | `42%` (+ file)   | 取消          | yes            |
| Indeterminate  | `12.3 MB`        | 下载中        | no             |
| Present        | (empty)          | 已下载        | no             |
| Error          | last error text  | 重试          | yes            |

The Indeterminate row appears only when the server omits Content-Length (see
ModelDownloader): no total, so no percent; show cumulative bytes instead and
disable the button. Owns one `ModelDownloader`. After `finished(ok)`,
re-evaluates state via `isModelPresent`. Emits nothing upward — the row is
self-contained; the active model is still chosen by the `model_name`
combobox.

### DTK custom widget factory

In `SettingsDialog` (or `EchoFlowSettings`), register:

```cpp
Dtk::Widget::DSettingsWidgetFactory::instance()->registerWidget(
    QStringLiteral("modeldownload"), [](QWidget* parent) -> QWidget* {
        // Actual factory overload receives the DSettingsOption*; read
        // option->data("model_id") -> findModel() -> ModelRowWidget(entry).
        return new ModelRowWidget(/* entry */, parent);
    });
```

The factory receives the `DSettingsOption*`; it reads
`option->data("model_id")` and looks up the `ModelEntry` via `findModel`, then
builds a `ModelRowWidget` for that entry. Decoupling the row from the option
key means reordering/renaming keys can't break the mapping. The exact DTK6
factory registration signature (`DSettingsWidgetFactory::registerWidget`) is
confirmed during implementation; if `data()` isn't reachable from the factory's
option handle, fall back to a key-suffix table (`download_0.6b`→catalog[0],
`download_1.7b`→catalog[1]) documented at the call site.

### `ui-host/settings-schema.json` changes

Under `basic.model`:

- `model_name` combobox: items become `qwen3-asr-0.6b`, `qwen3-asr-1.7b`
  (default `qwen3-asr-0.6b`).
- Two new options of `type: "modeldownload"`, each carrying the model id as
  option metadata so the factory never infers it from the key string:
  - `{ "key": "basic.model.download_0.6b", "name": "Qwen3-ASR-0.6B",
        "type": "modeldownload", "data": { "model_id": "qwen3-asr-0.6b" } }`
  - `{ "key": "basic.model.download_1.7b", "name": "Qwen3-ASR-1.7B",
        "type": "modeldownload", "data": { "model_id": "qwen3-asr-1.7b" } }`
  These have no persistent `value` (action widgets); if DTK requires a default,
  use an empty string and ignore it on save.
- New option `basic.model.mirror`, `type: "combobox"`, items `hf-mirror`,
  `official` (default `hf-mirror`), `name` "下载源".

Remove the `advanced.runtime.model_dir` option entirely.

### `ui-host/EchoFlowSettings.cpp` changes

- `populateComboBoxes()`: `model_name` items → `qwen3-asr-0.6b`,
  `qwen3-asr-1.7b`; add `basic.model.mirror` items `hf-mirror`, `official`.
- Default-config write list: replace `advanced.runtime.model_dir` with
  `basic.model.mirror`.

### `service/Config` changes

- `loadDtkConf`: stop reading the `advanced.runtime.model_dir` section. The
  derivation step **replaces** the current
  `cfg.modelDir = expandPath(cfg.modelDir, base);` line (Config.cpp) and the
  `if (cfg.modelDir == legacyDefaultModelDir())` rewrite — both are deleted, not
  left in place; otherwise `expandPath` would run on the now-empty `modelDir`
  and yield the config directory by accident. After parsing:
  `cfg.modelDir = (path.parent_path() / normalizeModelName(cfg.modelName)).string();`
  (`recordingsDir` still goes through `expandPath`; only `modelDir` changes
  path.) `Config::defaultConfig()` no longer sets a `modelDir`.
- `normalizeModelName(value)`: returns the canonical catalog id for recognized
  spellings — `qwen-asr-0.6b`, `0.6b`, `0.6B` → `qwen3-asr-0.6b`; `qwen-asr-1.7b`,
  `1.7b`, `1.7B` → `qwen3-asr-1.7b`. For any other input (including empty, or an
  uncatalogued id) it returns the input **unchanged** — it never invents a
  default. So an empty/unknown `modelName` yields an empty/unknown `modelDir`,
  which the self-test surfaces as an actionable message (see below).
- Remove `legacyDefaultModelDir()` entirely.
- `Config::defaultConfig()`: drop the `$HOME/AI/Model/qwen3-asr-0.6b` default;
  leave `modelDir` empty (it is derived at load time in `loadDtkConf`, which
  knows the config path). `Config::defaultConfig()` is only used when no config
  file exists at all; in that case `model_name` defaults to `qwen3-asr-0.6b`
  and the self-test will report the model as missing until the user downloads
  it from the UI.

### `service/main.cpp` changes

`--print-default-config` no longer prints a concrete `model_dir` (it would be
empty under `defaultConfig()`). Print only `model_name` and the other fields,
and append a note line explaining the model dir is derived as
`<config_dir>/<model_name>` at runtime.

### `service/SelfTest.cpp` changes

- `ModelCatalog` is the single owner of the file lists. SelfTest calls
  `missingModelFiles(dir, entry)` (the detailed vector) — not just
  `isModelPresent` — so the `[FAIL] model files present: …` line keeps listing
  exactly which files are missing. SelfTest's own `kRequiredModelFiles` and the
  hand-rolled large-shard branch are deleted.
- When `cfg.modelDir` is empty or `findModel(cfg.modelName)` returns null (fresh
  install, model not downloaded, or unknown `model_name`), emit an actionable
  line instead of a blank path, e.g.:
  `[FAIL] model: 未下载 — 打开 EchoFlow 设置 → 模型 下载 Qwen3-ASR-0.6B`.
  This is the path the Bug-2 case (`defaultConfig()` empty `modelDir`) hits on a
  brand-new install.
- `modelDirCandidates` / `resolveModelDir` `model-0.6B`/`model/` fallback is
  removed (legacy GGUF layout, no longer relevant). `resolveModelDir` becomes a
  trivial return of `cfg.modelDir`.

### `install-user.sh` changes

Default config block: remove the `[advanced.runtime.model_dir]` section; set
`model_name=qwen3-asr-0.6b`; add `[basic.model.mirror]\nvalue=hf-mirror`.

### `scripts/setup-qwen-asr-0.6b.sh` — removed.

Remove the `scripts/setup-qwen-asr-0.6b.sh` file. If `scripts/` is then empty,
remove the directory too (the spec test asserts the file is gone; an empty dir
is harmless but tidy).

## Tests

### `tests/test_config.cpp`

- Remove the `model_dir`-parsing assertions.
- Add: given a config with `model_name=qwen3-asr-1.7b` at path
  `/tmp/ef/conf/echoflow.conf`, `cfg.modelDir ==
  /tmp/ef/conf/qwen3-asr-1.7b`.
- Add: legacy `model_name=qwen-asr-0.6b` normalizes to `qwen3-asr-0.6b` and the
  derived dir follows.
- Add: a `[advanced.runtime.model_dir]` key in the config file is ignored
  (proves Bug 1 is fixed — no `expandPath` on it, path comes purely from
  `model_name`).
- Add `normalizeModelName` edge cases directly: empty → empty; unknown
  `something-else` → `something-else` unchanged; `0.6B` → `qwen3-asr-0.6b`.

### `tests/spec/run_spec.sh`

- Replace the three assertions on `scripts/setup-qwen-asr-0.6b.sh` with:
  `assert_absent "$ROOT/scripts/setup-qwen-asr-0.6b.sh"` (script removed).
- Add `assert_contains "$ROOT/ui-host/settings-schema.json" "modeldownload"`.
- Add `assert_contains "$ROOT/ui-host/settings-schema.json" "hf-mirror"`.
- Add `assert_contains` that both `Qwen3-ASR-0.6B` and `Qwen3-ASR-1.7B` display
  names are present.
- Add `assert_absent "$ROOT/ui-host/settings-schema.json" "model_dir"` and
  `assert_absent "$ROOT/install-user.sh" "model_dir"`.

### New logic test (QTest, links nothing Qt-heavy)

`tests/test_model_catalog.cpp` exercising the catalog:

- `isModelPresent`: empty dir → false for both; 0.6B dir with all five files →
  true; 1.7B dir missing one shard → false; 1.7B dir with both shards + index →
  true (even without a single `model.safetensors`).
- `missingModelFiles` returns the **named** missing files (proves Gap 5 —
  SelfTest keeps its diagnostic list), e.g. a 0.6B dir missing `vocab.json` and
  `merges.txt` yields exactly those two.

`ModelCatalog.h` is header-only and testable without Qt.

## Documentation

- `README.md`: replace the "准备模型" section — remove the `setup-qwen-asr-0.6b.sh`
  step; describe downloading from the settings dialog; note models now live
  under `~/.config/echoflow/`.
- `AGENTS.md`: drop the `./scripts/setup-qwen-asr-0.6b.sh` command; update the
  "Conventions & gotchas" model-path note; mention `ModelCatalog.h` as the
  shared source of truth and that download lives in `ui-host`.

## Risk / compatibility

- Existing configs with `model_name=qwen-asr-0.6b` (no `3`) are normalized on
  load, so the derived path resolves to the freshly downloaded
  `qwen3-asr-0.6b` directory.
- Existing configs with `advanced.runtime.model_dir` set: that key is now
  ignored. Users who pointed at a custom location must re-download from the UI
  (one click) or accept the default config-dir location. Documented as a
  breaking change in the README.
- Models previously downloaded to `$HOME/AI/Model/qwen3-asr-0.6b` are not
  auto-migrated; re-download is one click. Documented.
