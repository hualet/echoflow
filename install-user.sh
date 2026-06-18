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

uv venv "$STATE_DIR/.venv"
uv pip install --python "$STATE_DIR/.venv/bin/python" "$ROOT_DIR"

ASR_RUNNER="$STATE_DIR/.venv/bin/qwen-asr-transcribe"
if [[ ! -e "$CONFIG_DIR/echoflow.conf" ]]; then
  python3 - "$CONFIG_DIR" "$ASR_RUNNER" <<'PY'
import configparser
import sys
from pathlib import Path

config_dir = Path(sys.argv[1])
config_dir.mkdir(parents=True, exist_ok=True)
conf_path = config_dir / "echoflow.conf"
runner = sys.argv[2]

defaults = {
    "basic.model.model_name": "qwen-asr-0.6b",
    "basic.recognition.language": "Chinese",
    "basic.recognition.strip_trailing_punctuation": "false",
    "basic.recording.min_record_seconds": "0.25",
    "basic.recording.rate": "16000",
    "basic.recording.channels": "1",
    "basic.recording.format": "s16",
    "advanced.runtime.asr_project_dir": "$HOME/AI/Model/Qwen3-ASR-GGUF",
    "advanced.runtime.model_dir": "$HOME/AI/Model/Qwen3-ASR-GGUF/model-0.6B",
    "advanced.runtime.asr_runner": runner,
    "advanced.runtime.asr_timeout_seconds": "120",
    "advanced.fcitx.fcitx_commit": "true",
    "advanced.storage.recordings_dir": "$HOME/.local/share/echoflow/recordings",
}

parser = configparser.ConfigParser()
for key, value in defaults.items():
    parser.add_section(key)
    parser.set(key, "value", value)
with open(conf_path, "w", encoding="utf-8") as f:
    parser.write(f)
PY
fi

cmake -S "$ROOT_DIR/fcitx-addon" -B "$BUILD_DIR/fcitx-addon" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build "$BUILD_DIR/fcitx-addon"
cmake --install "$BUILD_DIR/fcitx-addon"

cmake -S "$ROOT_DIR/ui-host" -B "$BUILD_DIR/ui-host" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build "$BUILD_DIR/ui-host"
cmake --install "$BUILD_DIR/ui-host"

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

install -m 0644 "$ROOT_DIR/systemd/user/echoflow.service" "$SYSTEMD_USER_DIR/echoflow.service"
install -m 0644 "$ROOT_DIR/systemd/user/echoflow-ui.service" "$SYSTEMD_USER_DIR/echoflow-ui.service"

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
