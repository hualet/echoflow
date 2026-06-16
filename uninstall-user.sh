#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 HarryLoong
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

PREFIX="${PREFIX:-$HOME/.local}"
STATE_DIR="${STATE_DIR:-$HOME/.local/share/echoflow}"
SYSTEMD_USER_DIR="${SYSTEMD_USER_DIR:-$HOME/.config/systemd/user}"

systemctl --user disable --now echoflow.service echoflow-ui.service 2>/dev/null || true
rm -f "$SYSTEMD_USER_DIR/echoflow.service" "$SYSTEMD_USER_DIR/echoflow-ui.service"
systemctl --user daemon-reload 2>/dev/null || true

rm -f "$HOME/.local/share/fcitx5/addon/echoflow.conf"
rm -f "$PREFIX/bin/echoflow-ui"
rm -f "$PREFIX/lib/fcitx5/libechoflow.so"
rm -f "$PREFIX/lib/x86_64-linux-gnu/fcitx5/libechoflow.so"

echo "EchoFlow user services and binaries removed."
echo "Preserved state/config:"
echo "  $STATE_DIR"
echo "  $HOME/.config/echoflow"
