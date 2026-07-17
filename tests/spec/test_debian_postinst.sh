#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TEST_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/echoflow-postinst-spec.XXXXXX")"
trap 'rm -rf "$TEST_ROOT"' EXIT

if [[ ! -d /run/systemd/system ]]; then
  echo "ok   - Debian postinst runtime test skipped: /run/systemd/system absent"
  exit 0
fi

FAKE_BIN="$TEST_ROOT/fake-bin"
TEST_LOG="$TEST_ROOT/commands.log"
mkdir -p "$FAKE_BIN"
export TEST_LOG

cat > "$FAKE_BIN/deb-systemd-invoke" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf 'deb-systemd-invoke %s\n' "$*" >> "$TEST_LOG"
if [[ "${DEB_SYSTEMD_FAIL:-0}" == "1" ]]; then
  exit 42
fi
EOF
chmod +x "$FAKE_BIN/deb-systemd-invoke"

fail() {
  echo "FAIL - $*" >&2
  exit 1
}

assert_log() {
  local expected="$1"
  diff -u <(printf '%s\n' "$expected") "$TEST_LOG" || fail "unexpected postinst commands"
}

: > "$TEST_LOG"
PATH="$FAKE_BIN:/usr/bin:/bin" sh "$ROOT/debian/postinst" configure
assert_log $'deb-systemd-invoke --user daemon-reload\ndeb-systemd-invoke --user restart echoflow.service echoflow-ui.service'

: > "$TEST_LOG"
PATH="$FAKE_BIN:/usr/bin:/bin" sh "$ROOT/debian/postinst" triggered
[[ ! -s "$TEST_LOG" ]] || fail "unrelated postinst action invoked systemd"

: > "$TEST_LOG"
DPKG_ROOT="$TEST_ROOT/root" PATH="$FAKE_BIN:/usr/bin:/bin" \
  sh "$ROOT/debian/postinst" configure
[[ ! -s "$TEST_LOG" ]] || fail "DPKG_ROOT postinst invoked host systemd"

: > "$TEST_LOG"
DEB_SYSTEMD_FAIL=1 PATH="$FAKE_BIN:/usr/bin:/bin" \
  sh "$ROOT/debian/postinst" configure
assert_log $'deb-systemd-invoke --user daemon-reload\ndeb-systemd-invoke --user restart echoflow.service echoflow-ui.service'

echo "ok   - Debian postinst user-service restart behavior"
