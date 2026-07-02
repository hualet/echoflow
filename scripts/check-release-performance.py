#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from scripts.release_performance import run_release_check  # noqa: E402


def parse_args():
    parser = argparse.ArgumentParser(description="Compare EchoFlow release performance")
    parser.add_argument("--baseline-ref", required=True)
    parser.add_argument("--candidate-ref", default="HEAD")
    parser.add_argument("--version", required=True)
    parser.add_argument("--config", required=True)
    parser.add_argument("--model", required=True)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--iterations", type=int, default=3)
    parser.add_argument("--threads", type=int, default=6)
    parser.add_argument("--openblas-threads", type=int, default=4)
    parser.add_argument("--previous-evidence")
    parser.add_argument("--output", required=True)
    return parser.parse_args()


def main():
    try:
        run_release_check(parse_args())
    except (OSError, RuntimeError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
