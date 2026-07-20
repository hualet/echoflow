# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later

import copy
import json
import subprocess
import tempfile
import unittest
from pathlib import Path

from scripts import release_evidence
from tests.test_performance_gate import IDENTITY, result


class ReleaseEvidenceTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.repo = Path(self.temporary.name)
        self.git("init", "-q")
        self.git("config", "user.name", "Test")
        self.git("config", "user.email", "test@example.com")
        (self.repo / "debian").mkdir()
        (self.repo / "debian/changelog").write_text(
            "echoflow (0.2.1) unstable; urgency=medium\n\n  * Baseline.\n\n -- Test <test@example.com>  Wed, 01 Jul 2026 00:00:00 +0800\n",
            encoding="utf-8",
        )
        (self.repo / "service").mkdir()
        (self.repo / "service/main.cpp").write_text("baseline\n", encoding="utf-8")
        self.git("add", ".")
        self.git("commit", "-q", "-m", "baseline")
        self.git("tag", "v0.2.1")
        (self.repo / "service/main.cpp").write_text("candidate\n", encoding="utf-8")
        self.git("add", ".")
        self.git("commit", "-q", "-m", "candidate")
        self.candidate = self.git("rev-parse", "HEAD").strip()

    def tearDown(self):
        self.temporary.cleanup()

    def git(self, *args):
        return subprocess.run(
            ["git", *args], cwd=self.repo, check=True, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout

    def evidence(self, *, version="0.2.2", baseline_ref="v0.2.1"):
        baseline = result()
        candidate = result((105.0, 105.0))
        comparison = {
            "passed": True,
            "failures": [],
            "metrics": {
                "aggregate_latency_limit": 1.10,
                "sample_latency_limit": 1.20,
                "baseline_median_ms": 100.0,
                "candidate_median_ms": 105.0,
                "aggregate_ratio": 1.05,
                "sample_ratios": {"normal": 1.05},
                "baseline_cer": 0.0,
                "candidate_cer": 0.0,
            },
        }
        return {
            "schema_version": 1,
            "status": "pass",
            "version": version,
            "baseline": {
                "ref": baseline_ref,
                "commit": self.git("rev-parse", baseline_ref).strip(),
                "mode": "rebuilt",
                "identity": copy.deepcopy(IDENTITY),
                "result": baseline,
            },
            "candidate": {
                "ref": "HEAD",
                "commit": self.candidate,
                "identity": copy.deepcopy(IDENTITY),
                "result": candidate,
            },
            "comparison": comparison,
        }

    def make_release(self, *, evidence=None, source_change=False,
                     version="0.2.2"):
        changelog = self.repo / "debian/changelog"
        changelog.write_text(
            f"echoflow ({version}) unstable; urgency=medium\n\n  * Candidate.\n\n -- Test <test@example.com>  Thu, 02 Jul 2026 00:00:00 +0800\n\n"
            + changelog.read_text(encoding="utf-8"),
            encoding="utf-8",
        )
        if evidence is not None:
            directory = self.repo / "docs/performance/releases"
            directory.mkdir(parents=True)
            (directory / f"{version}.json").write_text(
                json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
        if source_change:
            (self.repo / "service/main.cpp").write_text("smuggled\n", encoding="utf-8")
        self.git("add", ".")
        self.git("commit", "-q", "-m", "release")
        return self.git("rev-parse", "HEAD").strip()

    def test_valid_release_commit_passes(self):
        commit = self.make_release(evidence=self.evidence())
        release_evidence.verify_release(self.repo, commit)

    def test_uses_latest_release_tag_when_changelog_skipped_versions(self):
        self.git("tag", "v0.2.2", self.candidate)
        self.git("tag", "v0.2.3", self.candidate)
        evidence = self.evidence(version="0.2.4", baseline_ref="v0.2.3")
        commit = self.make_release(evidence=evidence, version="0.2.4")
        release_evidence.verify_release(self.repo, commit)

    def test_missing_evidence_is_rejected(self):
        commit = self.make_release(evidence=None)
        with self.assertRaisesRegex(ValueError, "missing performance evidence"):
            release_evidence.verify_release(self.repo, commit)

    def test_wrong_candidate_parent_is_rejected(self):
        evidence = self.evidence()
        evidence["candidate"]["commit"] = evidence["baseline"]["commit"]
        commit = self.make_release(evidence=evidence)
        with self.assertRaisesRegex(ValueError, "candidate commit"):
            release_evidence.verify_release(self.repo, commit)

    def test_release_commit_source_change_is_rejected(self):
        commit = self.make_release(evidence=self.evidence(), source_change=True)
        with self.assertRaisesRegex(ValueError, "disallowed file"):
            release_evidence.verify_release(self.repo, commit)

    def test_reused_identity_mismatch_is_rejected(self):
        evidence = self.evidence()
        evidence["baseline"]["mode"] = "reused"
        evidence["baseline"]["identity"]["cpu_model"] = "Different CPU"
        commit = self.make_release(evidence=evidence)
        with self.assertRaisesRegex(ValueError, "benchmark identity"):
            release_evidence.verify_release(self.repo, commit)

    def test_tampered_comparison_is_rejected(self):
        evidence = self.evidence()
        evidence["candidate"]["result"]["samples"][0]["latency_ms"] = [130.0, 130.0]
        commit = self.make_release(evidence=evidence)
        with self.assertRaisesRegex(ValueError, "comparison does not match"):
            release_evidence.verify_release(self.repo, commit)


if __name__ == "__main__":
    unittest.main()
