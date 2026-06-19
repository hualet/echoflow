# Model download progress persistence — Design

- Date: 2026-06-19
- Status: Approved (brainstorm)
- Scope: Keep a model download running in the background when the EchoFlow
  settings dialog is closed, and restore live progress when the dialog is
  reopened. Today closing the dialog destroys the `ModelRowWidget` (and with
  it its `ModelDownloader`), aborting the HTTP transfer and wiping the
  in-memory progress state.

## Goal / user stories

- As a user, I can start downloading a multi-hundred-MB / 1.7 GB model, close
  the settings dialog to get on with my work, and the download keeps running
  inside the tray host.
- As a user, when I reopen the settings dialog while a download is in flight,
  I see the current percent (or downloaded MB in indeterminate mode) and a
  working 取消 button — the progress did not reset to 0.
- As a user, when a download finishes while the dialog is closed, reopening it
  shows 已下载 and the model_name combobox lists the newly available model.
- As a user, cancelling a download (with the dialog open) behaves exactly as
  today: status reads 已取消, button becomes 重试.

## Non-goals (YAGNI)

- No pause/resume across process restarts or HTTP Range byte-resume. Closing
  the **tray app** still abandons `.part` fragments (re-download starts from
  0 next time, same as a crash today). Only the **dialog** close is survived.
- No per-file progress breakdown; aggregate percent + current file unchanged.
- No download queue / global "one download at a time" limit. Each model id
  stays independent (the 0.6B and 1.7B rows may both run concurrently, exactly
  as today).
- No tray-icon / out-of-dialog progress surface. Progress is only visible
  inside the settings dialog.
- No checksum verification; presence + non-zero size only (unchanged).
- No persistence of progress to disk; the in-memory snapshot in the
  coordinator is enough since the tray host outlives the dialog.

## Decisions locked in brainstorm

1. **Background-continue, not pause/abort.** Closing the dialog must not stop
   the HTTP transfer; only the view is dismissed. Reopening reattaches to the
   live transfer. (Aborting and merely remembering a percent would freeze the
   bar — useless for a long download.)
2. **Approach B — long-lived coordinator + dialog stays destroy-on-close.**
   A new `ModelDownloadCoordinator` singleton owns the active downloaders for
   the lifetime of the tray host; `ModelRowWidget` becomes a pure view that
   reattaches on each construction. Rejected alternatives:
   - *Approach C (hide instead of destroy the dialog)*: ~6 lines, but couples
     download lifetime to "dialog opened once", keeps the whole widget tree in
     memory, and can show stale disk state (e.g. files deleted externally)
     without an extra `showEvent` refresh. Approach B rebuilds widgets every
     open so `refreshState()` always re-checks disk.
   - *Approach A (full download manager with queue / cancel-all)*: YAGNI for
     at most two concurrent model downloads.
3. **`ModelDownloader` is essentially unchanged.** It is already a
   self-contained, signal-driven unit; the bug was purely that its parent was
   a dialog-lifetime widget. All new logic lives in the coordinator and the
   view-ified row widget.
4. **An in-flight download keeps its original mirror.** `baseUrl` is captured
   at `start()` time and handed to the `ModelDownloader`, which never re-reads
   it. Changing the 下载源 combobox while a download is running does **not**
   retarget the live transfer (impossible mid-stream anyway); it only affects
   the next `start()`. This matches today's behavior and keeps the coordinator
   settings-agnostic.

## Architecture & boundaries

- **Download lifetime is separated from view lifetime.** `ModelDownloader`
  moves from being parented to a `ModelRowWidget` (dialog lifetime) to being
  parented to `ModelDownloadCoordinator` (tray-host lifetime). The dialog's
  `WA_DeleteOnClose` semantics are preserved, so every open rebuilds widgets
  from fresh disk state.
- **The coordinator is the view's only dependency.** `ModelRowWidget` never
  touches a `ModelDownloader*` directly; it queries the coordinator's cached
  snapshot and connects to the coordinator's id-tagged signals. This keeps
  the view trivial and means the coordinator can outlive any number of
  widget constructions.
- **The coordinator stays settings-agnostic.** It receives a resolved
  `baseUrl` from the caller; the mirror→URL mapping (`baseUrlFromMirror()`)
  stays in `ModelRowWidget` where it already reads `EchoFlowSettings` live.
- **`EchoFlowSettings` is not touched.** The coordinator is a peer singleton,
  not folded into the config singleton, so config I/O and HTTP download
  ownership stay in separate classes (matching the existing
  `ModelDownloader`/`EchoFlowSettings` split).

## Components

### `ui-host/ModelDownloadCoordinator.{h,cpp}` (new)

`QObject` singleton accessed via `ModelDownloadCoordinator::instance()`,
mirroring `EchoFlowSettings::instance()`. First-touch construction happens
inside the settings dialog (well after `QApplication` exists, so the
underlying `QNetworkAccessManager` is safe). Reparenting downloaders to it
(`new ModelDownloader(..., this)`) means they live until the tray host exits
or until they finish.

```cpp
namespace echoflow {

enum class DownloadState { Idle, Downloading, Succeeded, Failed };

struct DownloadSnapshot {
    DownloadState state = DownloadState::Idle;
    qint64 done = 0;
    qint64 total = 0;     // 0 == indeterminate (no Content-Length)
    QString currentFile;
    QString error;        // set on Failed; "已取消" for user cancel
};

class ModelDownloadCoordinator : public QObject {
    Q_OBJECT
public:
    static ModelDownloadCoordinator* instance();

    // No-op if id already has an active downloader. The entry supplies id +
    // file list; dir + baseUrl come from the caller (view resolves mirror).
    void start(const ModelEntry& entry, const QString& dir, const QString& baseUrl);
    void cancel(const QString& id);

    DownloadSnapshot snapshot(const QString& id) const;

signals:
    void progress(const QString& id, qint64 done, qint64 total, const QString& file);
    void stateChanged(const QString& id, DownloadState state, const QString& error);

private:
    // One shared NAM is not worth the churn; each downloader keeps its own
    // (existing behavior), parented to the downloader which is parented here.
    QHash<QString, ModelDownloader*> active_;
    QHash<QString, DownloadSnapshot> cache_;
};

}  // namespace echoflow
```

Behavior:

- `start`: if `active_.contains(id)`, ignore (already running). Otherwise
  create `auto* d = new ModelDownloader(entry, dir, baseUrl, this);`, store in
  `active_`, set `cache_[id] = {Downloading, 0, 0, "", ""}`, connect `d`'s
  `progress` → update `cache_[id].done/total/currentFile` and emit
  `progress(id, ...)`; connect `d`'s `finished(ok, err)` → set
  `cache_[id].state = ok ? Succeeded : Failed`, `cache_[id].error = err`,
  emit `stateChanged(id, state, err)`, `d->deleteLater()`, remove from
  `active_` (cache entry retained so a subsequent `snapshot()` reflects the
  terminal result until the next `start` overwrites it). Then `d->start()`.
- `cancel(id)`: if active, forward `cancel()` (the downloader emits
  `finished(false, "已取消")`, handled by the `finished` slot above).
- `snapshot(id)`: returns `cache_.value(id)` (default-constructed `Idle` if
  never started). A view reads this on construction to paint immediately,
  because it will have missed any `progress` signals emitted before it
  connected.

### `ui-host/ModelDownloader.{h,cpp}` (unchanged)

No API change. Its `progress(done, total, file)` and `finished(ok, error)`
signals are exactly what the coordinator caches. It keeps owning its own
`QNetworkAccessManager` (per-downloader NAM, as today).

### `ui-host/ModelRowWidget` (view-ified)

Drops the `ModelDownloader* downloader_` member and its `new`/`deleteLater`
ownership. Talks only to `ModelDownloadCoordinator::instance()`:

- Construction (after the existing label/button setup):
  - `const auto snap = coordinator->snapshot(id);`
  - If `snap.state == Downloading`: render from `snap` (% if `total > 0`, else
    MB), button "取消", enabled; then connect signals.
  - Else: `refreshState()` as today (disk check: Missing → "下载" / Present →
    "已下载"). The terminal `Failed` snapshot is intentionally **not** restored
    here — on reopen, a clean disk check is less confusing than a stale error
    from a previous session. (Live errors during an open dialog still surface
    via `stateChanged`.)
- `connect(coordinator, progress, this, [id-filter] onCoordinatorProgress)`;
  same for `stateChanged`. The id filter is a guard at the top of each Qt
  lambda (both rows share the coordinator's signals, each keeps only its own):

  ```cpp
  connect(c, &ModelDownloadCoordinator::progress, this,
          [this](const QString& id, qint64 done, qint64 total, const QString& f) {
              if (id != QString::fromStdString(entry_->id)) return;
              // …render %  or MB, button stays 取消 + enabled
          });
  ```

  Qt auto-disconnects when `this` is destroyed, so no manual cleanup is needed
  even though the coordinator outlives the widget.
- `onClicked`:
  - If `snapshot(id).state == Downloading` → `coordinator->cancel(id)`.
  - Else → `coordinator->start(*entry_, dir, baseUrlFromMirror())`, set
    button "取消", clear status.
- `onCoordinatorProgress(id, done, total, file)` (filtered by id): identical
  rendering to today's `onProgress` — `total > 0` → percent, else MB; button
  stays "取消" + enabled.
- `onCoordinatorStateChanged(id, state, error)` (filtered by id):
  - `Downloading` → button "取消" enabled (defensive; progress drives numbers).
  - `Succeeded` → `refreshState()` (disk → "已下载") +
    `EchoFlowSettings::instance()->refreshModelNameItems()`.
  - `Failed` → `status_->setText(error)`, button "重试", enabled.
    (`error == "已取消"` reproduces today's exact cancelled look.)

`configDir()` and `baseUrlFromMirror()` helpers stay where they are
(`ModelRowWidget.cpp` anonymous namespace).

### `ui-host/main.cpp` — reopen refresh

In the `openSettings` lambda, before `settingsDialog->show()`, call
`EchoFlowSettings::instance()->refreshModelNameItems()`. This covers the case
where a download **completed while the dialog was closed**: the model_name
combobox is rebuilt from disk on every open, so the just-finished model is
immediately selectable. (When the dialog is open at completion time, the
widget's `stateChanged(Succeeded)` handler calls the same method live.)

The dialog stays `Qt::WA_DeleteOnClose`; the `destroyed` lambda and the
`settingsDialog` pointer dance in `main.cpp:317-331` are otherwise unchanged.

### `ui-host/CMakeLists.txt`

Add `ModelDownloadCoordinator.cpp` to the `echoflow-ui` sources.

## State / data flow

```
User clicks 下载 (ModelRowWidget)
  → coordinator->start(entry, dir, baseUrl)
  → new ModelDownloader(parent=coordinator); cache[id]=Downloading; d->start()
  → (HTTP) d emits progress(done,total,file)
       → coordinator updates cache[id], emits progress(id,...)
       → all live ModelRowWidgets with matching id repaint
  → close dialog: ModelRowWidget destroyed; downloader keeps running (parent=coordinator)
  → reopen dialog: new ModelRowWidget reads snapshot(id)=Downloading → repaints live %, reconnects
  → d emits finished(ok,err)
       → coordinator cache[id]=Succeeded|Failed, emits stateChanged
       → live widget: refreshState() + refreshModelNameItems (Succeeded) / error+重试 (Failed)
       → (if dialog closed: no widget; refreshModelNameItems runs on next openSettings)
       → d->deleteLater(), removed from active_; cache retained
```

## Testing

- **No new QNetwork unit test.** The coordinator is a thin Qt-network-bound
  layer over `ModelDownloader`, which itself is not unit-tested today (real
  HTTP, no mock seam). Introducing an `IModelDownloader` interface purely for
  a test would violate YAGNI.
- **`tests/spec/run_spec.sh` structural assertions** (the helpers available are
  `assert_contains` / `assert_absent` / `assert_script_absent`; file existence
  is enforced transitively by the build failing if a source is missing):
  - `assert_contains "$ROOT/ui-host/CMakeLists.txt" "ModelDownloadCoordinator.cpp"`
    — the new source is wired into `echoflow-ui`.
  - `assert_contains "$ROOT/ui-host/ModelRowWidget.cpp" "ModelDownloadCoordinator"`
    — the view talks to the coordinator (proves the refactor landed).
  - `assert_absent "$ROOT/ui-host/ModelRowWidget.cpp" "new ModelDownloader"` —
    proves the widget no longer owns/constructs a downloader directly.
- **Manual verification** (the real proof):
  1. Start the 0.6B download, note percent.
  2. Close the settings dialog.
  3. Wait ~30s, reopen. Percent advanced, did not reset to 0, 取消 works.
  4. Let it finish while closed; reopen → 已下载 + model_name lists 0.6B.
  5. Repeat with a flaky mirror (or pull network) to confirm Failed → 重试.

## Documentation

- No `AGENTS.md` boundary change needed — download still lives in `ui-host`,
  and the coordinator is another small `ui-host` class. The "download lives in
  ui-host" note remains accurate.
- No README change: user-visible behavior is "download keeps going when you
  close settings", which is the intuitive expectation, not worth a callout.

## Risk / compatibility

- **Static-singleton QObject**: `ModelDownloadCoordinator::instance()` uses a
  function-local static, identical to `EchoFlowSettings`. Destruction at
  process exit; child downloaders destroyed first. The project already
  accepts this pattern, so no new risk.
- **Missed progress signals**: a widget constructed mid-download cannot see
  past `progress` emissions. Mitigated by the `snapshot()` cache, which the
  widget reads at construction before connecting — it paints the current
  percent immediately and is driven by signals thereafter.
- **Stale terminal snapshot**: after `Failed`, `cache_[id]` retains the error.
  The view ignores it on construction (falls back to disk check), so a stale
  error never shows on reopen; only a live `stateChanged` during an open
  dialog surfaces it. The next `start(id)` overwrites the snapshot.
- **No new HTTP semantics**: `.part` cleanup, indeterminate mode, short-write
  protection, and the sizing pre-flight are all in the untouched
  `ModelDownloader`; behavior is identical to today, only the lifetime/owner
  changes.
