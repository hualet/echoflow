#!/bin/sh
# SPDX-FileCopyrightText: 2026 HarryLoong
# SPDX-License-Identifier: GPL-3.0-or-later

set -eu

if [ ! -d .venv ]; then
    uv venv
fi

uv run echoflow-service "$@"
