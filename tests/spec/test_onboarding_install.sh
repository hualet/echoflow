#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TEST_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/echoflow-install-spec.XXXXXX")"
trap 'rm -rf "$TEST_ROOT"' EXIT

FAKE_BIN="$TEST_ROOT/fake-bin"
TEST_LOG="$TEST_ROOT/commands.log"
mkdir -p "$FAKE_BIN"
export TEST_LOG

cat > "$FAKE_BIN/cmake" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf 'cmake' >> "$TEST_LOG"
printf ' <%s>' "$@" >> "$TEST_LOG"
printf '\n' >> "$TEST_LOG"

if [[ "${1:-}" == "-S" ]]; then
  build_dir=""
  for ((i = 1; i <= $#; ++i)); do
    if [[ "${!i}" == "-B" ]]; then
      next=$((i + 1))
      build_dir="${!next}"
    fi
  done
  mkdir -p "$build_dir/systemd/user"
  : > "$build_dir/systemd/user/echoflow.service"
  : > "$build_dir/systemd/user/echoflow-ui.service"
elif [[ "${1:-}" == "--install" ]]; then
  mkdir -p "$PREFIX/bin" "$PREFIX/lib/fcitx5"
  : > "$PREFIX/bin/echoflow-service"
  : > "$PREFIX/bin/echoflow-ui"
  : > "$PREFIX/lib/fcitx5/libechoflow.so"
fi
EOF

cat > "$FAKE_BIN/systemctl" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf 'systemctl %s\n' "$*" >> "$TEST_LOG"
if [[ "${FAIL_RESTART:-0}" == "1" && "$*" == "--user restart echoflow.service echoflow-ui.service" ]]; then
  exit 23
fi
EOF

cat > "$FAKE_BIN/fcitx5" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf 'fcitx5 %s\n' "$*" >> "$TEST_LOG"
EOF
chmod +x "$FAKE_BIN/cmake" "$FAKE_BIN/systemctl" "$FAKE_BIN/fcitx5"

fail() {
  echo "FAIL - $*" >&2
  exit 1
}

assert_logged() {
  grep -qFx -- "$1" "$TEST_LOG" || fail "missing command: $1"
}

assert_not_logged() {
  if grep -qF -- "$1" "$TEST_LOG"; then
    fail "unexpected command: $1"
  fi
}

assert_ordered() {
  local previous=0 needle line
  for needle in "$@"; do
    line="$(grep -nF -- "$needle" "$TEST_LOG" | head -1 | cut -d: -f1)"
    [[ -n "$line" && "$line" -gt "$previous" ]] || fail "command order at: $needle"
    previous="$line"
  done
}

run_install() {
  local home="$1"
  shift
  HOME="$home" \
  PREFIX="$home/prefix with spaces" \
  STATE_DIR="$home/state" \
  CONFIG_DIR="$home/config" \
  SYSTEMD_USER_DIR="$home/systemd" \
  BUILD_DIR="$home/build with spaces" \
  PATH="$FAKE_BIN:/usr/bin:/bin" \
    bash "$ROOT/install-user.sh" "$@"
}

DEFAULT_HOME="$TEST_ROOT/default-home"
: > "$TEST_LOG"
default_output="$(run_install "$DEFAULT_HOME")"
assert_logged "systemctl --user daemon-reload"
assert_logged "systemctl --user enable echoflow.service echoflow-ui.service"
assert_logged "systemctl --user restart echoflow.service echoflow-ui.service"
assert_logged "fcitx5 -rd"
assert_ordered \
  "systemctl --user daemon-reload" \
  "systemctl --user enable echoflow.service echoflow-ui.service" \
  "systemctl --user restart echoflow.service echoflow-ui.service" \
  "fcitx5 -rd"
grep -qF 'EchoFlow installed.' <<< "$default_output" || fail "default install did not report success"

NO_START_HOME="$TEST_ROOT/no-start-home"
: > "$TEST_LOG"
no_start_output="$(run_install "$NO_START_HOME" --no-start)"
assert_logged "systemctl --user daemon-reload"
assert_logged "systemctl --user enable echoflow.service echoflow-ui.service"
assert_not_logged "systemctl --user restart"
assert_not_logged "fcitx5"
grep -qF 'Start services later:' <<< "$no_start_output" || fail "no-start guidance missing"

FAIL_HOME="$TEST_ROOT/failure-home"
: > "$TEST_LOG"
failure_output="$TEST_ROOT/failure.out"
if FAIL_RESTART=1 run_install "$FAIL_HOME" > "$failure_output" 2>&1; then
  fail "restart failure returned success"
fi
assert_logged "systemctl --user restart echoflow.service echoflow-ui.service"
assert_not_logged "fcitx5"
if grep -qF 'EchoFlow installed.' "$failure_output"; then
  fail "restart failure printed install success"
fi

UNINSTALL_HOME="$TEST_ROOT/uninstall-home"
UNINSTALL_PREFIX="$UNINSTALL_HOME/prefix with spaces"
mkdir -p \
  "$UNINSTALL_PREFIX/share/applications" \
  "$UNINSTALL_PREFIX/share/icons/hicolor/scalable/apps" \
  "$UNINSTALL_HOME/state" \
  "$UNINSTALL_HOME/.config/echoflow"
: > "$UNINSTALL_PREFIX/share/applications/echoflow.desktop"
: > "$UNINSTALL_PREFIX/share/icons/hicolor/scalable/apps/echoflow.svg"
: > "$UNINSTALL_HOME/state/preserved"
: > "$UNINSTALL_HOME/.config/echoflow/preserved"
: > "$TEST_LOG"
HOME="$UNINSTALL_HOME" \
PREFIX="$UNINSTALL_PREFIX" \
STATE_DIR="$UNINSTALL_HOME/state" \
SYSTEMD_USER_DIR="$UNINSTALL_HOME/systemd" \
PATH="$FAKE_BIN:/usr/bin:/bin" \
  bash "$ROOT/uninstall-user.sh" > "$TEST_ROOT/uninstall.out"
[[ ! -e "$UNINSTALL_PREFIX/share/applications/echoflow.desktop" ]] || fail "desktop entry not removed"
[[ ! -e "$UNINSTALL_PREFIX/share/icons/hicolor/scalable/apps/echoflow.svg" ]] || fail "icon not removed"
[[ -e "$UNINSTALL_HOME/state/preserved" ]] || fail "state was removed"
[[ -e "$UNINSTALL_HOME/.config/echoflow/preserved" ]] || fail "config was removed"

echo "ok   - executable install and uninstall behavior"
