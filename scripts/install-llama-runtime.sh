#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

MODEL_ROOT="${MODEL_ROOT:-$HOME/AI/Model}"
QWEN_ASR_PROJECT_DIR="${QWEN_ASR_PROJECT_DIR:-$MODEL_ROOT/Qwen3-ASR-GGUF}"
LLAMA_BUILD_DIR="${LLAMA_BUILD_DIR:-$MODEL_ROOT/llama.cpp-build/build}"
TARGET_DIR="$QWEN_ASR_PROJECT_DIR/qwen_asr_gguf/inference/bin"

usage() {
  cat <<EOF
Usage: $0

Environment:
  MODEL_ROOT            Base model directory. Default: $HOME/AI/Model
  QWEN_ASR_PROJECT_DIR  Qwen3-ASR-GGUF project directory.
  LLAMA_BUILD_DIR       Existing llama.cpp build directory.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

find_required_library() {
  local pattern="$1"
  local found
  found="$(find "$LLAMA_BUILD_DIR" -type f -name "$pattern" -print -quit 2>/dev/null || true)"
  if [[ -z "$found" ]]; then
    echo "Missing $pattern under $LLAMA_BUILD_DIR" >&2
    return 1
  fi
}

if [[ ! -d "$QWEN_ASR_PROJECT_DIR/qwen_asr_gguf/inference" ]]; then
  echo "Qwen3-ASR-GGUF inference package not found: $QWEN_ASR_PROJECT_DIR/qwen_asr_gguf/inference" >&2
  exit 1
fi
if [[ ! -d "$LLAMA_BUILD_DIR" ]]; then
  echo "llama.cpp build directory not found: $LLAMA_BUILD_DIR" >&2
  exit 1
fi

find_required_library "libllama*.so*"
find_required_library "libggml*.so*"

mkdir -p "$TARGET_DIR"
while IFS= read -r library; do
  cp -a "$library" "$TARGET_DIR/"
done < <(find "$LLAMA_BUILD_DIR" \( -name 'libllama*.so*' -o -name 'libggml*.so*' \) -print)

echo "Installed llama.cpp runtime libraries to $TARGET_DIR"
