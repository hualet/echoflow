#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

DEST="${DEST:-$HOME/AI/Model/qwen3-asr-0.6b}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
QWEN_ASR="$ROOT_DIR/third_party/qwen-asr"

if [[ ! -d "$QWEN_ASR" ]]; then
  echo "qwen-asr submodule missing; run: git submodule update --init --recursive third_party/qwen-asr" >&2
  exit 1
fi

mkdir -p "$DEST"
bash "$QWEN_ASR/download_model.sh" --model small --dir "$DEST"
echo "Model installed at $DEST"
