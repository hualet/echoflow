#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

PREFIX="${PREFIX:-$HOME/.local}"
CACHE_DIR="${CACHE_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/echoflow/sensevoice-runtime}"
BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
SENSEVOICE_COMMIT="${SENSEVOICE_COMMIT:-266faeaf9a72bf1072c49b7743f0008c5630acfb}"
LLAMA_COMMIT="${LLAMA_COMMIT:-8086439a4cea94c71a5dfb8fe4ad1546aebd640f}"

BIN="$PREFIX/bin/llama-funasr-sensevoice"
if [[ -x "$BIN" ]]; then
  echo "SenseVoice runtime already installed: $BIN"
  exit 0
fi

mkdir -p "$CACHE_DIR" "$PREFIX/bin"

download_tarball() {
  local url="$1"
  local out="$2"
  if [[ -s "$out" ]]; then
    return
  fi
  curl -L --fail --show-error -o "$out" "$url"
}

SENSEVOICE_TGZ="$CACHE_DIR/SenseVoice-$SENSEVOICE_COMMIT.tar.gz"
LLAMA_TGZ="$CACHE_DIR/llama.cpp-$LLAMA_COMMIT.tar.gz"
SENSEVOICE_SRC="$CACHE_DIR/SenseVoice-src"
LLAMA_SRC="$CACHE_DIR/llama-src"
BUILD_DIR="$CACHE_DIR/build"

download_tarball \
  "https://github.com/FunAudioLLM/SenseVoice/archive/$SENSEVOICE_COMMIT.tar.gz" \
  "$SENSEVOICE_TGZ"
download_tarball \
  "https://github.com/ggml-org/llama.cpp/archive/$LLAMA_COMMIT.tar.gz" \
  "$LLAMA_TGZ"

if [[ ! -f "$SENSEVOICE_SRC/runtime/llama.cpp/CMakeLists.txt" ]]; then
  rm -rf "$SENSEVOICE_SRC"
  mkdir -p "$SENSEVOICE_SRC"
  tar -xzf "$SENSEVOICE_TGZ" -C "$SENSEVOICE_SRC" --strip-components=1
fi

if [[ ! -f "$LLAMA_SRC/CMakeLists.txt" ]]; then
  rm -rf "$LLAMA_SRC"
  mkdir -p "$LLAMA_SRC"
  tar -xzf "$LLAMA_TGZ" -C "$LLAMA_SRC" --strip-components=1
fi

cmake -S "$SENSEVOICE_SRC/runtime/llama.cpp" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DGGML_NATIVE=ON \
  -DLLAMA_CURL=OFF \
  -DFETCHCONTENT_SOURCE_DIR_LLAMA="$LLAMA_SRC"
cmake --build "$BUILD_DIR" --target llama-funasr-sensevoice -j"$(nproc)"
install -m 0755 "$BUILD_DIR/bin/llama-funasr-sensevoice" "$BIN"
echo "SenseVoice runtime installed: $BIN"
