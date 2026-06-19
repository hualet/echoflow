# Model download progress persistence — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Keep a model download running in the background when the EchoFlow settings dialog is closed, and restore live progress when the dialog is reopened.

**Architecture:** Introduce a tray-host-lifetime singleton `ModelDownloadCoordinator` that owns the active `ModelDownloader` instances and caches each download's latest `(state, done, total, file, error)` snapshot. `ModelRowWidget` becomes a pure view that queries the snapshot at construction and reconnects to the coordinator's id-tagged signals — so closing the (still destroy-on-close) dialog no longer kills the transfer or wipes progress. `ModelDownloader` itself is unchanged.

**Tech Stack:** C++17, Qt6 (Core/Network/Widgets), DTK6, CMake, QTest + bash spec-as-test.

**Spec:** `docs/superpowers/specs/2026-06-19-model-download-progress-persistence-design.md`

---

## File map

- **Create:** `ui-host/ModelDownloadCoordinator.h` — singleton coordinator: owns active `ModelDownloader*` per model id, caches snapshots, re-emits id-tagged signals.
- **Create:** `ui-host/ModelDownloadCoordinator.cpp` — implementation.
- **Modify:** `ui-host/ModelRowWidget.h` — drop `ModelDownloader*` member; slots now take id-tagged coordinator signals.
- **Modify:** `ui-host/ModelRowWidget.cpp` — rewrite from owner to pure view of the coordinator.
- **Modify:** `ui-host/main.cpp` — call `refreshModelNameItems()` on each `openSettings` (covers "finished while closed").
- **Modify:** `ui-host/CMakeLists.txt` — add the new source.
- **Modify:** `tests/spec/run_spec.sh` — structural assertions for the refactor.

---

## Task 1: Create `ModelDownloadCoordinator`

**Files:**
- Create: `ui-host/ModelDownloadCoordinator.h`
- Create: `ui-host/ModelDownloadCoordinator.cpp`
- Modify: `ui-host/CMakeLists.txt:18-26`

- [ ] **Step 1: Write the header `ui-host/ModelDownloadCoordinator.h`**

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_MODEL_DOWNLOAD_COORDINATOR_H
#define ECHOFLOW_MODEL_DOWNLOAD_COORDINATOR_H

#include "ModelCatalog.h"

#include <QObject>
#include <QString>
#include <QHash>

namespace echoflow {

class ModelDownloader;

enum class DownloadState { Idle, Downloading, Succeeded, Failed };

// Cached view of one model's download, sufficient for a freshly-constructed
// ModelRowWidget to paint immediately without having seen prior progress
// signals (which it missed, having just been created).
struct DownloadSnapshot {
    DownloadState state = DownloadState::Idle;
    qint64 done = 0;
    qint64 total = 0;       // 0 == indeterminate (no Content-Length)
    QString currentFile;
    QString error;          // set on Failed; "已取消" for a user cancel
};

// Owns the active ModelDownloader instances for the lifetime of the tray host,
// so a download outlives the settings dialog being closed and reopened. The
// row widget never touches a ModelDownloader directly: it reads snapshot() and
// connects to progress()/stateChanged(). Reparenting downloaders to this
// singleton (new ModelDownloader(..., this)) is what detaches their lifetime
// from the dialog's.
class ModelDownloadCoordinator : public QObject {
    Q_OBJECT
public:
    static ModelDownloadCoordinator* instance();

    // No-op if id already has an active downloader. The entry supplies id +
    // file list; dir + baseUrl come from the caller (the view resolves the
    // mirror so the coordinator stays settings-agnostic). An in-flight
    // download keeps its original baseUrl; changing the mirror only affects
    // the next start().
    void start(const ModelEntry& entry, const QString& dir, const QString& baseUrl);
    void cancel(const QString& id);

    DownloadSnapshot snapshot(const QString& id) const;

signals:
    // Both carry the model id so each ModelRowWidget keeps only its own.
    void progress(const QString& id, qint64 done, qint64 total, const QString& file);
    void stateChanged(const QString& id, DownloadState state, const QString& error);

private:
    explicit ModelDownloadCoordinator(QObject* parent = nullptr);

    QHash<QString, ModelDownloader*> active_;
    QHash<QString, DownloadSnapshot> cache_;
};

}  // namespace echoflow

#endif  // ECHOFLOW_MODEL_DOWNLOAD_COORDINATOR_H
```

- [ ] **Step 2: Write the implementation `ui-host/ModelDownloadCoordinator.cpp`**

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ModelDownloadCoordinator.h"

#include "ModelDownloader.h"

namespace echoflow {

ModelDownloadCoordinator::ModelDownloadCoordinator(QObject* parent)
    : QObject(parent) {}

ModelDownloadCoordinator* ModelDownloadCoordinator::instance() {
    static ModelDownloadCoordinator coordinator;
    return &coordinator;
}

void ModelDownloadCoordinator::start(const ModelEntry& entry,
                                     const QString& dir,
                                     const QString& baseUrl) {
    const QString id = QString::fromStdString(entry.id);
    if (active_.contains(id)) {
        return;  // already running — ignore double-start
    }

    // Parented to `this` (the tray-host-lifetime singleton), not to any widget.
    auto* d = new ModelDownloader(entry, dir, baseUrl, this);
    active_.insert(id, d);
    cache_[id] = DownloadSnapshot{DownloadState::Downloading, 0, 0, QString(), QString()};

    // Each downloader's signals are connected with a lambda capturing its id,
    // so the coordinator can fan out one set of signals to N widgets without
    // sender() lookup.
    connect(d, &ModelDownloader::progress, this,
            [this, id](qint64 done, qint64 total, const QString& file) {
                cache_[id].done = done;
                cache_[id].total = total;
                cache_[id].currentFile = file;
                emit progress(id, done, total, file);
            });
    connect(d, &ModelDownloader::finished, this,
            [this, id](bool ok, const QString& error) {
                cache_[id].state = ok ? DownloadState::Succeeded : DownloadState::Failed;
                cache_[id].error = ok ? QString() : error;
                emit stateChanged(id, cache_[id].state, error);
                // Terminal: drop ownership. The snapshot is retained so a
                // widget opened right after sees the result until the next
                // start() overwrites it.
                auto it = active_.find(id);
                if (it != active_.end()) {
                    it.value()->deleteLater();
                    active_.erase(it);
                }
            });

    d->start();
}

void ModelDownloadCoordinator::cancel(const QString& id) {
    auto it = active_.find(id);
    if (it != active_.end()) {
        // cancel() emits finished(false, "已取消") synchronously; the finished
        // lambda above handles the cache/signal/cleanup.
        it.value()->cancel();
    }
}

DownloadSnapshot ModelDownloadCoordinator::snapshot(const QString& id) const {
    return cache_.value(id);
}

}  // namespace echoflow
```

- [ ] **Step 3: Add the source to `ui-host/CMakeLists.txt`**

In the `add_executable(echoflow-ui ...)` list, add `ModelDownloadCoordinator.cpp` immediately after `ModelDownloader.cpp`. The block becomes:

```cmake
add_executable(echoflow-ui
    main.cpp
    EchoFlowSettings.cpp
    SettingsDialog.cpp
    ModelDownloader.cpp
    ModelDownloadCoordinator.cpp
    ModelRowWidget.cpp
    settings.qrc
    qml.qrc
)
```

- [ ] **Step 4: Build to verify it compiles**

Run: `cmake --build build`
Expected: builds cleanly. Nothing references `ModelDownloadCoordinator::instance()` yet, but the class compiles and links into `echoflow-ui`.

- [ ] **Step 5: Commit**

```bash
git add ui-host/ModelDownloadCoordinator.h ui-host/ModelDownloadCoordinator.cpp ui-host/CMakeLists.txt
git commit -m "Add ModelDownloadCoordinator

Tray-host-lifetime singleton that owns the active ModelDownloader instances
and caches each download's latest snapshot. Decouples download lifetime
from the settings dialog so a transfer survives the dialog being closed;
the row widget is rewritten to use it in the next change."
```

---

## Task 2: Rewrite `ModelRowWidget` as a coordinator view (+ spec assertions)

This task is structured test-first for the structural invariants: the new `run_spec.sh` assertions fail against the current owner-style widget, then the rewrite makes them pass.

**Files:**
- Modify: `tests/spec/run_spec.sh` (append three assertions)
- Modify: `ui-host/ModelRowWidget.h`
- Modify: `ui-host/ModelRowWidget.cpp`

- [ ] **Step 1: Add the failing spec assertions**

Append these lines to `tests/spec/run_spec.sh` (after the existing `ModelCatalog`/`EchoFlowSettings` block, before the final summary):

```bash
assert_contains "$ROOT/ui-host/CMakeLists.txt" "ModelDownloadCoordinator.cpp" "ui-host builds ModelDownloadCoordinator"
assert_contains "$ROOT/ui-host/ModelRowWidget.cpp" "ModelDownloadCoordinator" "ModelRowWidget talks to the coordinator"
assert_absent  "$ROOT/ui-host/ModelRowWidget.cpp" "new ModelDownloader" "ModelRowWidget no longer owns a downloader"
```

- [ ] **Step 2: Run the spec test to verify two assertions FAIL**

Run: `ctest --test-dir build -R spec --output-on-failure` (or `bash tests/spec/run_spec.sh`)
Expected: the first assertion (`CMakeLists` contains `ModelDownloadCoordinator.cpp`) PASSES (done in Task 1); the other two FAIL:
- `ModelRowWidget talks to the coordinator` — `ModelRowWidget.cpp` does not yet reference `ModelDownloadCoordinator`.
- `ModelRowWidget no longer owns a downloader` — `ModelRowWidget.cpp` still contains `new ModelDownloader`.

- [ ] **Step 3: Rewrite `ui-host/ModelRowWidget.h`**

Replace the entire file with:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_MODEL_ROW_WIDGET_H
#define ECHOFLOW_MODEL_ROW_WIDGET_H

#include "ModelCatalog.h"
#include "ModelDownloadCoordinator.h"

#include <QWidget>

class QLabel;
class QPushButton;

namespace echoflow {

// Pure view of one model's download. Owns no downloader; it queries
// ModelDownloadCoordinator for the current snapshot and connects to its
// id-tagged signals. Constructed fresh every time the (destroy-on-close)
// settings dialog opens, so it always reflects live progress if a download is
// in flight, or fresh disk state otherwise.
class ModelRowWidget : public QWidget {
    Q_OBJECT
public:
    explicit ModelRowWidget(const ModelEntry* entry, QWidget* parent = nullptr);

private slots:
    void onClicked();
    void onCoordinatorProgress(const QString& id, qint64 done, qint64 total, const QString& file);
    void onCoordinatorStateChanged(const QString& id, DownloadState state, const QString& error);

private:
    void refreshState();
    void renderProgress(qint64 done, qint64 total);
    QString modelId() const;

    const ModelEntry* entry_;
    QLabel* status_ = nullptr;
    QPushButton* button_ = nullptr;
};

}  // namespace echoflow

#endif  // ECHOFLOW_MODEL_ROW_WIDGET_H
```

- [ ] **Step 4: Rewrite `ui-host/ModelRowWidget.cpp`**

Replace the entire file with:

```cpp
// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ModelRowWidget.h"

#include "EchoFlowSettings.h"
#include "ModelDownloadCoordinator.h"

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

// Read the mirror setting live (so changing the combobox takes effect on the
// NEXT start; an in-flight download keeps the mirror it was started with).
QString baseUrlFromMirror() {
    auto* ds = EchoFlowSettings::instance()->dsettings();
    QString mirror = QStringLiteral("hf-mirror");
    if (ds) {
        auto opt = ds->option(QStringLiteral("basic.model.mirror"));
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

    auto* c = ModelDownloadCoordinator::instance();
    // Both rows share the coordinator's signals; each slot filters by id.
    // Qt auto-disconnects when `this` is destroyed, so reopening the dialog
    // (which deletes the old widget) needs no manual cleanup.
    connect(c, &ModelDownloadCoordinator::progress, this,
            &ModelRowWidget::onCoordinatorProgress);
    connect(c, &ModelDownloadCoordinator::stateChanged, this,
            &ModelRowWidget::onCoordinatorStateChanged);

    // Paint immediately from the snapshot — the widget missed any progress
    // signals emitted before it existed.
    const DownloadSnapshot snap = c->snapshot(modelId());
    if (snap.state == DownloadState::Downloading) {
        renderProgress(snap.done, snap.total);
        button_->setText(QStringLiteral("取消"));
        button_->setEnabled(true);
    } else {
        refreshState();
    }
}

QString ModelRowWidget::modelId() const {
    return entry_ ? QString::fromStdString(entry_->id) : QString();
}

void ModelRowWidget::renderProgress(qint64 done, qint64 total) {
    if (total > 0) {
        const int pct = static_cast<int>((done * 100) / total);
        status_->setText(QString::number(qBound(0, pct, 100)) + QStringLiteral("%"));
    } else {
        // Indeterminate (no Content-Length): show downloaded megabytes.
        const double mb = done / (1024.0 * 1024.0);
        status_->setText(QString("%1 MB").arg(mb, 0, 'f', 1));
    }
}

void ModelRowWidget::refreshState() {
    if (!entry_) {
        status_->setText(QString());
        button_->setText(QStringLiteral("未知"));
        button_->setEnabled(false);
        return;
    }
    // A download in flight is driven by the coordinator signals, not disk.
    if (ModelDownloadCoordinator::instance()->snapshot(modelId()).state
        == DownloadState::Downloading) {
        return;
    }
    const QString dir = configDir() + QStringLiteral("/") + modelId();
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
    if (!entry_) {
        return;
    }
    auto* c = ModelDownloadCoordinator::instance();
    if (c->snapshot(modelId()).state == DownloadState::Downloading) {
        // Download in flight: the button is the cancel affordance.
        c->cancel(modelId());  // coordinator emits stateChanged(Failed, "已取消")
        return;
    }
    const QString dir = configDir() + QStringLiteral("/") + modelId();
    c->start(*entry_, dir, baseUrlFromMirror());
    button_->setText(QStringLiteral("取消"));
    button_->setEnabled(true);
    status_->setText(QString());
}

void ModelRowWidget::onCoordinatorProgress(const QString& id, qint64 done, qint64 total,
                                           const QString& /*file*/) {
    if (id != modelId()) {
        return;
    }
    // Throughout a download the button is the cancel affordance and stays
    // enabled — including indeterminate mode and indeterminate->determinate
    // transitions (matches the pre-refactor behavior).
    button_->setText(QStringLiteral("取消"));
    button_->setEnabled(true);
    renderProgress(done, total);
}

void ModelRowWidget::onCoordinatorStateChanged(const QString& id, DownloadState state,
                                               const QString& error) {
    if (id != modelId()) {
        return;
    }
    if (state == DownloadState::Downloading) {
        button_->setText(QStringLiteral("取消"));
        button_->setEnabled(true);
        return;
    }
    if (state == DownloadState::Failed) {
        // Covers user cancel too: the downloader emits finished(false,"已取消"),
        // so the status reads 已取消 and the button becomes 重试.
        status_->setText(error);
        button_->setText(QStringLiteral("重试"));
        button_->setEnabled(true);
        return;
    }
    // Succeeded (or Idle): re-evaluate against disk; a model was (possibly)
    // added, so rebuild the model_name combobox so it becomes selectable.
    refreshState();
    EchoFlowSettings::instance()->refreshModelNameItems();
}

}  // namespace echoflow
```

- [ ] **Step 5: Build**

Run: `cmake --build build`
Expected: builds cleanly. `ModelDownloader.h` is no longer included here (the widget never instantiates one); the coordinator header supplies `DownloadState`/`DownloadSnapshot`.

- [ ] **Step 6: Run the spec test to verify all assertions now PASS**

Run: `ctest --test-dir build -R spec --output-on-failure`
Expected: all three new assertions pass (plus the rest of the suite unchanged).

- [ ] **Step 7: Run the full test suite**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: 100% tests pass.

- [ ] **Step 8: Commit**

```bash
git add tests/spec/run_spec.sh ui-host/ModelRowWidget.h ui-host/ModelRowWidget.cpp
git commit -m "Make ModelRowWidget a coordinator view

The widget no longer owns a ModelDownloader; it queries the coordinator's
snapshot at construction and reconnects to its id-tagged signals. Closing
the settings dialog therefore no longer destroys the transfer or wipes
progress — reopening reattaches to the live download. Cancel maps to the
coordinator; succeeded/failed/cancelled states reproduce the prior look."
```

---

## Task 3: Refresh `model_name` items on every dialog open

Covers the case where a download **completes while the dialog is closed**: without this, the model_name combobox would not list the newly available model until the next restart.

**Files:**
- Modify: `ui-host/main.cpp:318-331` (the `openSettings` lambda)

- [ ] **Step 1: Add the refresh call**

In `ui-host/main.cpp`, inside the `openSettings` lambda, add one line immediately before `settingsDialog->show();` so the block reads:

```cpp
    auto openSettings = [&]() {
        if (!settingsDialog) {
            settingsDialog = new echoflow::SettingsDialog(
                echoflow::EchoFlowSettings::instance()->dsettings());
            settingsDialog->setAttribute(Qt::WA_DeleteOnClose);
            QObject::connect(settingsDialog, &QObject::destroyed, [&]() {
                echoflow::EchoFlowSettings::instance()->sync();
                settingsDialog = nullptr;
            });
        }
        echoflow::EchoFlowSettings::instance()->refreshModelNameItems();
        settingsDialog->show();
        settingsDialog->raise();
        settingsDialog->activateWindow();
    };
```

- [ ] **Step 2: Build**

Run: `cmake --build build`
Expected: builds cleanly.

- [ ] **Step 3: Commit**

```bash
git add ui-host/main.cpp
git commit -m "Refresh model_name items on each settings open

A download can finish while the dialog is closed (the coordinator keeps it
alive). Rebuild the combobox from disk on every open so the just-finished
model is immediately selectable without a restart."
```

---

## Task 4: Final verification

No code changes — this is the proof that the feature behaves end-to-end. The behavioral layer (Qt HTTP) is not unit-tested by project convention; manual verification is the test.

- [ ] **Step 1: Clean rebuild + full test suite**

Run:
```bash
cmake --build build && ctest --test-dir build --output-on-failure
```
Expected: 100% tests pass, including the three new spec assertions.

- [ ] **Step 2: CLI sanity (unchanged behavior)**

Run: `./build/service/echoflow-service --print-default-config`
Expected: prints config; the `model_dir` line reads "derived at runtime" (unchanged from before this change — we touched only the UI host).

- [ ] **Step 3: Manual — progress survives dialog close**

This step requires a running desktop with PipeWire/Fcitx and at least one model **not** yet downloaded. If both models are already present, delete one model dir under `~/.config/echoflow/` first.

1. `./run.sh` (starts the tray host + service).
2. Tray menu → 设置. On the missing model's row, click 下载. Note the percent once it is > 0.
3. Close the settings dialog (window close button — triggers `WA_DeleteOnClose`).
4. Wait ~30 seconds. Reopen 设置.
   - **Expected:** the row shows an *advanced* percent (> the value from step 2), button reads 取消 and is enabled. Progress did **not** reset to 0.
5. Click 取消. **Expected:** status reads 已取消, button reads 重试.
6. Click 重试 (restarts via the coordinator). Close the dialog. Let it finish (watch `ls ~/.config/echoflow/<model-id>/` fill up).
7. Reopen 设置. **Expected:** row reads 已下载 (button disabled), and the model_name combobox now lists that model (Task 3).
8. Quit the tray app.

- [ ] **Step 4: Note the scope boundary**

Confirm by reading `ui-host/ModelDownloader.{h,cpp}` that those files were **not** modified by this plan (only their parent/owner changed). If they were touched, something went off-script — re-check.

---

## Self-review notes

- **Spec coverage:** every component in the spec maps to a task — coordinator (T1), `ModelRowWidget` view (T2), `ModelDownloadCoordinator` API `start/cancel/snapshot/progress/stateChanged` (T1 code), main.cpp reopen-refresh (T3), `ModelDownloader` unchanged (T4 step 4 verifies), spec assertions (T2). The two brainstorm nitpicks (id-filter lambda syntax, mirror-change-during-download) are reflected in the T1/T2 code and the coordinator header comment.
- **Type consistency:** `DownloadSnapshot`, `DownloadState`, signal signatures, and `snapshot()` are identical across T1 (definition) and T2 (consumer). `modelId()`, `renderProgress()` helpers introduced in T2.h are used consistently in T2.cpp.
- **No placeholders:** every code step shows complete code; every command shows expected output.
