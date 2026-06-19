# Repository Guidelines

## Project Structure & Module Organization

EchoFlow is an offline voice input method for deepin/Fcitx5. The top-level
`CMakeLists.txt` builds native components. `service/` contains the daemon,
recording lifecycle, ASR integration, sockets, commit client, UI notifications,
and self-test logic. `fcitx-addon/` captures right `Ctrl`, tracks input-context
events, and talks to the daemon. `ui-host/` contains the Qt6/DTK tray and
settings UI, while `qml/` holds tooltip QML.
`qwen-asr-runtime/` wraps `third_party/qwen-asr/`. Tests live in `tests/`, with
shell spec checks in `tests/spec/` and performance benchmarks in
`tests/benchmarks/`.

## Build, Test, and Development Commands

Initialize fresh clones with:

```bash
git submodule update --init --recursive third_party/qwen-asr
```

Configure, build, and run the full test suite:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

Sanity checks:

```bash
./build/service/echoflow-service --print-default-config
./build/service/echoflow-service --self-test
bash -n install-user.sh uninstall-user.sh tests/spec/*.sh
sh -n run.sh
```

Run from the build tree with `./run.sh`; install locally with
`./install-user.sh` or `./install-user.sh --no-start`.

## Coding Style & Naming Conventions

Use C++17 and existing local interfaces. Classes are `PascalCase`, methods are
`camelCase`, members use trailing underscores, and constants use `kCamelCase`.
Every source file must include SPDX copyright and GPL license headers. Keep
qwen-asr C API usage isolated to `service/AsrEngine.*`; do not
include `qwen_asr.h` elsewhere. Prefer concise comments only for non-obvious
behavior.

## Testing Guidelines

Logic tests are QTest binaries in `tests/*.cpp` and link
`libechoflow_service.a`. Tests must not require model weights, PipeWire capture,
or a running Fcitx daemon. Add focused tests for service logic through
`IRecorder`, `IAsrEngine`, `ICommitter`, and `IUiNotifier` boundaries. Run
`ctest --test-dir build --output-on-failure` before submitting changes.

## Commit & Pull Request Guidelines

Use Conventional Commits for each subject, for example
`fix(service): handle ASR setup failure`. Each commit must include a body that
explains why the change is needed and what changed, wrapped near 72 columns.
Before committing, run `git status --short` and stage only intended source,
test, and documentation files. If the work generated spec or plan documents
under `docs/superpowers/specs/` or `docs/superpowers/plans/`, include those in
the same commit. Do not stage model weights, recordings, build directories, or
installed artifacts. Pull requests should describe behavior changes,
verification commands, and manual runtime steps such as `fcitx5 -rd` after
addon changes.

## Configuration & Runtime Notes

Runtime sockets live under `/run/user/$UID/`: `echoflow-control.sock`,
`echoflow-fcitx.sock`, and `echoflow-ui.sock`. Config lives at
`~/.config/echoflow/echoflow.conf`; `Config::modelDir` is derived from the
selected model and config directory. `service/ModelCatalog.h` is the single
source of truth for model IDs, display names, Hugging Face repos, and file
lists. The service must not perform HTTP downloads; model download belongs in
`ui-host`.
