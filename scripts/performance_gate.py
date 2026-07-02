#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later

"""Deterministic comparison logic for EchoFlow release benchmarks."""

import argparse
import json
import statistics
import sys
from pathlib import Path


IDENTITY_FIELDS = (
    "cpu_model",
    "architecture",
    "cpu_flags",
    "build_profile",
    "benchmark_schema",
    "model_sha256",
    "manifest_sha256",
    "config_sha256",
    "threads",
    "openblas_threads",
)

AGGREGATE_LATENCY_LIMIT = 1.10
SAMPLE_LATENCY_LIMIT = 1.20


def _identity_value(identity, field):
    if field not in identity:
        return None
    value = identity[field]
    if field == "cpu_flags":
        return sorted(value)
    return value


def identities_match(left, right):
    """Return true only when every field needed for reuse is present and equal."""
    return all(
        field in left
        and field in right
        and _identity_value(left, field) == _identity_value(right, field)
        for field in IDENTITY_FIELDS
    )


def _sample_map(result):
    samples = result.get("samples", [])
    mapped = {sample.get("id"): sample for sample in samples}
    if None in mapped or len(mapped) != len(samples):
        raise ValueError("sample IDs must be present and unique")
    return mapped


def _median(sample):
    values = sample.get("latency_ms", [])
    if not values:
        raise ValueError(f"sample has no latency iterations: {sample['id']}")
    return statistics.median(float(value) for value in values)


def compare_results(baseline, candidate):
    """Compare two normalized result objects and return auditable gate metrics."""
    baseline_by_id = _sample_map(baseline)
    candidate_by_id = _sample_map(candidate)
    failures = []
    if baseline_by_id.keys() != candidate_by_id.keys():
        failures.append("sample_set")

    ratios = {}
    baseline_medians = []
    candidate_medians = []
    shared_ids = sorted(baseline_by_id.keys() & candidate_by_id.keys())
    for sample_id in shared_ids:
        before_sample = baseline_by_id[sample_id]
        after_sample = candidate_by_id[sample_id]
        if before_sample.get("sha256") != after_sample.get("sha256"):
            failures.append(f"sample_hash:{sample_id}")
        before = _median(before_sample)
        after = _median(after_sample)
        if before <= 0:
            raise ValueError(f"baseline latency must be positive: {sample_id}")
        ratio = after / before
        ratios[sample_id] = ratio
        baseline_medians.append(before)
        candidate_medians.append(after)
        if ratio > SAMPLE_LATENCY_LIMIT:
            failures.append(f"sample_latency:{sample_id}")
        if after_sample.get("required", True) and not after_sample.get("transcript", "").strip():
            failures.append(f"empty_transcript:{sample_id}")

    if not shared_ids:
        raise ValueError("results have no shared samples")
    baseline_aggregate = statistics.median(baseline_medians)
    candidate_aggregate = statistics.median(candidate_medians)
    aggregate_ratio = candidate_aggregate / baseline_aggregate
    if aggregate_ratio > AGGREGATE_LATENCY_LIMIT:
        failures.append("aggregate_latency")

    baseline_cer = float(baseline.get("aggregate_cer", 0.0))
    candidate_cer = float(candidate.get("aggregate_cer", 0.0))
    if candidate_cer > baseline_cer:
        failures.append("aggregate_cer")

    return {
        "passed": not failures,
        "failures": failures,
        "metrics": {
            "aggregate_latency_limit": AGGREGATE_LATENCY_LIMIT,
            "sample_latency_limit": SAMPLE_LATENCY_LIMIT,
            "baseline_median_ms": baseline_aggregate,
            "candidate_median_ms": candidate_aggregate,
            "aggregate_ratio": aggregate_ratio,
            "sample_ratios": ratios,
            "baseline_cer": baseline_cer,
            "candidate_cer": candidate_cer,
        },
    }


def _load(path):
    with Path(path).open(encoding="utf-8") as stream:
        return json.load(stream)


def main(argv=None):
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    compare = subparsers.add_parser("compare")
    compare.add_argument("baseline")
    compare.add_argument("candidate")
    compare.add_argument("--output", required=True)
    args = parser.parse_args(argv)

    comparison = compare_results(_load(args.baseline), _load(args.candidate))
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(comparison, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if not comparison["passed"]:
        print("performance gate failed: " + ", ".join(comparison["failures"]), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
