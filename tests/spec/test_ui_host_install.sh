#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TEST_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/echoflow-ui-install-spec.XXXXXX")"
trap 'rm -rf "$TEST_ROOT"' EXIT

for command_name in cmake desktop-file-validate; do
  if ! command -v "$command_name" >/dev/null 2>&1; then
    echo "FAIL - required command unavailable: $command_name" >&2
    exit 1
  fi
done

NATIVE_BUILD="$TEST_ROOT/native-build"
CUSTOM_BUILD="$TEST_ROOT/custom-build"
DESTDIR_ROOT="$TEST_ROOT/destdir"
CUSTOM_PREFIX="/opt/Echo Flow"

cmake -S "$ROOT/ui-host" -B "$NATIVE_BUILD" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX=/usr >/dev/null
grep -qFx 'Exec="/usr/bin/echoflow-ui" --activate' \
  "$NATIVE_BUILD/echoflow.desktop" || {
    echo "FAIL - native desktop Exec is not /usr/bin" >&2
    exit 1
  }

cmake -S "$ROOT/ui-host" -B "$CUSTOM_BUILD" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX="$CUSTOM_PREFIX" >/dev/null
cmake --build "$CUSTOM_BUILD" -j2 >/dev/null
DESTDIR="$DESTDIR_ROOT" cmake --install "$CUSTOM_BUILD" >/dev/null

INSTALLED_DESKTOP="$DESTDIR_ROOT$CUSTOM_PREFIX/share/applications/echoflow.desktop"
INSTALLED_ICON="$DESTDIR_ROOT$CUSTOM_PREFIX/share/icons/hicolor/scalable/apps/echoflow.svg"
INSTALLED_BINARY="$DESTDIR_ROOT$CUSTOM_PREFIX/bin/echoflow-ui"

[[ -x "$INSTALLED_BINARY" ]] || { echo "FAIL - standalone UI binary not installed" >&2; exit 1; }
[[ -f "$INSTALLED_DESKTOP" ]] || { echo "FAIL - desktop entry not installed" >&2; exit 1; }
[[ -f "$INSTALLED_ICON" ]] || { echo "FAIL - icon not installed" >&2; exit 1; }
grep -qFx 'Exec="/opt/Echo Flow/bin/echoflow-ui" --activate' "$INSTALLED_DESKTOP" || {
  echo "FAIL - custom-prefix desktop Exec is incorrect" >&2
  exit 1
}
desktop-file-validate "$INSTALLED_DESKTOP"
if grep -qF -- "$ROOT" "$INSTALLED_DESKTOP"; then
  echo "FAIL - installed desktop entry contains source path" >&2
  exit 1
fi

echo "ok   - standalone UI configure, build, and DESTDIR install"
