#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from scripts.release_evidence import verify_release  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description="Verify EchoFlow release performance evidence")
    parser.add_argument("--commit", default="HEAD")
    parser.add_argument("--tag")
    args = parser.parse_args()
    try:
        result = verify_release(Path.cwd(), args.commit, args.tag)
    except (ValueError, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        return 1
    if result["skipped"]:
        print(f"no version bump at {args.commit}; performance evidence check skipped")
    else:
        print(f"verified performance evidence for {result['version']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
