# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later

import copy
import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from scripts import release_performance as driver
from tests.test_performance_gate import IDENTITY


class ReleasePerformanceDriverTest(unittest.TestCase):
    def test_matching_identity_builds_only_candidate(self):
        plan = driver.make_build_plan(
            baseline_identity=copy.deepcopy(IDENTITY),
            current_identity=copy.deepcopy(IDENTITY),
            baseline_ref="v0.2.1",
            candidate_ref="HEAD",
        )
        self.assertEqual([(item.role, item.ref) for item in plan], [("candidate", "HEAD")])

    def test_cpu_mismatch_builds_both_revisions(self):
        baseline = copy.deepcopy(IDENTITY)
        baseline["cpu_model"] = "Different CPU"
        plan = driver.make_build_plan(
            baseline_identity=baseline,
            current_identity=copy.deepcopy(IDENTITY),
            baseline_ref="v0.2.1",
            candidate_ref="HEAD",
        )
        self.assertEqual(
            [(item.role, item.ref) for item in plan],
            [("baseline", "v0.2.1"), ("candidate", "HEAD")],
        )

    def test_missing_baseline_identity_builds_both_revisions(self):
        plan = driver.make_build_plan(
            baseline_identity=None,
            current_identity=copy.deepcopy(IDENTITY),
            baseline_ref="v0.2.1",
            candidate_ref="HEAD",
        )
        self.assertEqual([item.role for item in plan], ["baseline", "candidate"])

    def test_model_mismatch_builds_both_revisions(self):
        baseline = copy.deepcopy(IDENTITY)
        baseline["model_sha256"] = "f" * 64
        plan = driver.make_build_plan(
            baseline_identity=baseline,
            current_identity=copy.deepcopy(IDENTITY),
            baseline_ref="v0.2.1",
            candidate_ref="HEAD",
        )
        self.assertEqual([item.role for item in plan], ["baseline", "candidate"])

    def test_iterations_below_three_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "at least 3"):
            driver.validate_iterations(2)

    def test_iterations_of_three_are_accepted(self):
        self.assertEqual(driver.validate_iterations(3), 3)

    def test_manifest_audio_hash_is_verified(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            audio = root / "sample.wav"
            audio.write_bytes(b"wav-data")
            manifest = root / "manifest.json"
            manifest.write_text(json.dumps([{
                "id": "normal",
                "audio": "sample.wav",
                "sha256": hashlib.sha256(b"different").hexdigest(),
                "reference": "测试",
                "condition": "normal",
            }]), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "sha256 mismatch: normal"):
                driver.load_manifest(manifest)

    def test_manifest_paths_and_hashes_are_normalized(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            audio = root / "sample.wav"
            audio.write_bytes(b"wav-data")
            digest = hashlib.sha256(b"wav-data").hexdigest()
            manifest = root / "manifest.json"
            manifest.write_text(json.dumps([{
                "id": "normal",
                "audio": "sample.wav",
                "sha256": digest,
                "reference": "测试",
                "condition": "normal",
            }]), encoding="utf-8")
            loaded = driver.load_manifest(manifest)
            self.assertEqual(loaded[0]["audio"], str(audio.resolve()))
            self.assertEqual(loaded[0]["sha256"], digest)

    def test_compilation_in_second_build_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "incremental build compiled"):
            driver.verify_noop_build_output("[ 10%] Building CXX object service/a.cpp.o")

    def test_link_only_second_build_is_accepted(self):
        driver.verify_noop_build_output("[100%] Built target voice_latency_benchmark")


if __name__ == "__main__":
    unittest.main()
