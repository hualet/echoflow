#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 HarryLoong
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

MODEL_ROOT="${MODEL_ROOT:-$HOME/AI/Model}"
PROJECT_DIR="${QWEN_ASR_PROJECT_DIR:-$MODEL_ROOT/Qwen3-ASR-GGUF}"
MODEL_DIR="${QWEN_ASR_MODEL_DIR:-$PROJECT_DIR/model-0.6B}"
ZIP_PATH="$PROJECT_DIR/Qwen3-ASR-0.6B-gguf.zip"
MODEL_URL="${QWEN_ASR_MODEL_URL:-https://github.com/HaujetZhao/Qwen3-ASR-GGUF/releases/download/models/Qwen3-ASR-0.6B-gguf.zip}"
PROJECT_URL="${QWEN_ASR_PROJECT_URL:-https://github.com/HaujetZhao/Qwen3-ASR-GGUF.git}"

mkdir -p "$MODEL_ROOT"

if [[ ! -d "$PROJECT_DIR/.git" ]]; then
  if [[ -e "$PROJECT_DIR" ]]; then
    echo "Qwen ASR project exists but is not a git checkout: $PROJECT_DIR" >&2
    exit 1
  fi
  git clone "$PROJECT_URL" "$PROJECT_DIR"
fi

if [[ ! -e "$ZIP_PATH" ]]; then
  curl -L -o "$ZIP_PATH" "$MODEL_URL"
fi

mkdir -p "$MODEL_DIR"
unzip -o "$ZIP_PATH" -d "$MODEL_DIR"

if [[ -e "$MODEL_DIR/qwen3_asr_llm.q4_k.gguf" ]]; then
  (
    cd "$MODEL_DIR"
    ln -sf qwen3_asr_llm.q4_k.gguf qwen3_asr_llm.q5_k.gguf
  )
fi

cat <<EOF
Qwen ASR 0.6B prepared.

Project: $PROJECT_DIR
Model:   $MODEL_DIR

Next:
  1. Ensure llama.cpp shared libraries exist under:
     $PROJECT_DIR/qwen_asr_gguf/inference/bin/
  2. Set EchoFlow config:
     "asr_project_dir": "$PROJECT_DIR"
     "model_dir": "$MODEL_DIR"
  3. Run:
     echoflow-service --config <config.json> --self-test
EOF
