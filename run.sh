#!/bin/sh
# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later

set -eu

JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

if ! cmake --build build -j "$JOBS" 2>/dev/null; then
    cmake -S . -B build
    cmake --build build -j "$JOBS"
fi

exec ./build/service/echoflow-service "$@"
