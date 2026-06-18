#!/bin/sh
# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later

set -eu

if ! cmake --build build 2>/dev/null; then
    cmake -S . -B build
    cmake --build build
fi

exec ./build/service/echoflow-service "$@"
