#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later

"""Model-free verification for EchoFlow release performance evidence."""

import json
import re
import subprocess
from pathlib import Path

from scripts.performance_gate import compare_results, identities_match


def _git(repo, *args):
    result = subprocess.run(
        ["git", *args], cwd=repo, check=False, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        raise ValueError(f"git {' '.join(args)} failed: {result.stderr.strip()}")
    return result.stdout


def _version(changelog):
    match = re.match(r"^echoflow \((\d+\.\d+\.\d+)\) ", changelog)
    if not match:
        raise ValueError("cannot parse native Debian version from changelog")
    return match.group(1)


def _commit_file(repo, commit, path):
    try:
        return _git(repo, "show", f"{commit}:{path}")
    except ValueError as error:
        raise ValueError(f"missing performance evidence: {path}") from error


def verify_release(repo, commit="HEAD", tag=None):
    repo = Path(repo)
    commit = _git(repo, "rev-parse", f"{commit}^{{commit}}").strip()
    parent = _git(repo, "rev-parse", f"{commit}^").strip()
    version = _version(_commit_file(repo, commit, "debian/changelog"))
    previous_version = _version(_commit_file(repo, parent, "debian/changelog"))
    if version == previous_version:
        if tag:
            raise ValueError("release tag does not contain a version bump")
        return {"skipped": True, "version": version}

    evidence_path = f"docs/performance/releases/{version}.json"
    evidence = json.loads(_commit_file(repo, commit, evidence_path))
    allowed = {"debian/changelog", evidence_path}
    changed = set(_git(repo, "diff-tree", "--no-commit-id", "--name-only", "-r",
                       parent, commit).splitlines())
    disallowed = sorted(changed - allowed)
    if disallowed:
        raise ValueError("release commit changes disallowed file: " + ", ".join(disallowed))
    if changed != allowed:
        raise ValueError("release commit must change changelog and performance evidence only")

    if evidence.get("schema_version") != 1:
        raise ValueError("unsupported performance evidence schema")
    if evidence.get("status") != "pass":
        raise ValueError("performance evidence status is not pass")
    if evidence.get("version") != version:
        raise ValueError("performance evidence version does not match changelog")
    candidate = evidence.get("candidate", {})
    baseline = evidence.get("baseline", {})
    if candidate.get("commit") != parent:
        raise ValueError("evidence candidate commit is not the release commit parent")
    expected_baseline_ref = f"v{previous_version}"
    if baseline.get("ref") != expected_baseline_ref:
        raise ValueError(f"baseline ref must be {expected_baseline_ref}")
    baseline_commit = _git(repo, "rev-parse", f"{expected_baseline_ref}^{{commit}}").strip()
    if baseline.get("commit") != baseline_commit:
        raise ValueError("baseline commit does not match previous release tag")
    if baseline.get("mode") not in {"reused", "rebuilt"}:
        raise ValueError("baseline mode must be reused or rebuilt")
    if not identities_match(baseline.get("identity", {}), candidate.get("identity", {})):
        raise ValueError("baseline and candidate benchmark identity do not match")

    recalculated = compare_results(baseline.get("result", {}), candidate.get("result", {}))
    if evidence.get("comparison") != recalculated:
        raise ValueError("recorded comparison does not match raw benchmark results")
    if not recalculated["passed"]:
        raise ValueError("raw benchmark results fail the performance gate")

    if tag:
        expected_tag = f"v{version}"
        if tag != expected_tag:
            raise ValueError(f"release tag must be {expected_tag}")
        tagged_commit = _git(repo, "rev-parse", f"{tag}^{{commit}}").strip()
        if tagged_commit != commit:
            raise ValueError("release tag does not point to the verified commit")
    return {"skipped": False, "version": version, "evidence": evidence_path}
