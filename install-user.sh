#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${PREFIX:-$HOME/.local}"
STATE_DIR="${STATE_DIR:-$HOME/.local/share/echoflow}"
CONFIG_DIR="${CONFIG_DIR:-$HOME/.config/echoflow}"
SYSTEMD_USER_DIR="${SYSTEMD_USER_DIR:-$HOME/.config/systemd/user}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build/install-user}"
BUILD_TYPE="${CMAKE_BUILD_TYPE:-RelWithDebInfo}"
START_SERVICES=1

usage() {
  cat <<EOF
Usage: $0 [--no-start]

Options:
  --no-start  Install files and enable user services without starting them.
  -h, --help  Show this help.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-start)
      START_SERVICES=0
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
  shift
done

mkdir -p "$STATE_DIR" "$CONFIG_DIR" "$SYSTEMD_USER_DIR"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build "$BUILD_DIR"
cmake --install "$BUILD_DIR"

if [[ ! -e "$CONFIG_DIR/echoflow.conf" ]]; then
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
[advanced.runtime.openblas_threads]
value=4
[advanced.fcitx.fcitx_commit]
value=true
[advanced.storage.recordings_dir]
value=\$HOME/.local/share/echoflow/recordings
EOF
fi

ADDON_LIB="$PREFIX/lib/fcitx5/libechoflow.so"
if [[ ! -e "$ADDON_LIB" ]]; then
  ADDON_LIB="$(find "$PREFIX" -path '*/fcitx5/libechoflow.so' -print -quit)"
fi
if [[ -z "$ADDON_LIB" || ! -e "$ADDON_LIB" ]]; then
  echo "Could not find installed libechoflow.so under $PREFIX" >&2
  exit 1
fi

FCITX_ADDON_DIR="$HOME/.local/share/fcitx5/addon"
mkdir -p "$FCITX_ADDON_DIR"
sed "s|^Library=.*|Library=${ADDON_LIB%.so}|" \
  "$ROOT_DIR/fcitx-addon/echoflow.conf.in" > "$FCITX_ADDON_DIR/echoflow.conf"

install -m 0644 "$BUILD_DIR/systemd/user/echoflow.service" "$SYSTEMD_USER_DIR/echoflow.service"
install -m 0644 "$BUILD_DIR/systemd/user/echoflow-ui.service" "$SYSTEMD_USER_DIR/echoflow-ui.service"

systemctl --user daemon-reload
if [[ "$START_SERVICES" == "1" ]]; then
  systemctl --user enable --now echoflow.service echoflow-ui.service
else
  systemctl --user enable echoflow.service echoflow-ui.service
fi

echo "EchoFlow installed."
echo "Config: $CONFIG_DIR/echoflow.conf"
echo "Fcitx addon: $FCITX_ADDON_DIR/echoflow.conf"
echo "Restart Fcitx if needed: fcitx5 -rd"
if [[ "$START_SERVICES" == "0" ]]; then
  echo "Start services later: systemctl --user start echoflow.service echoflow-ui.service"
fi
