# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later

import copy
import hashlib
import json
import tempfile
import unittest
from pathlib import Path
import subprocess

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

    def test_finds_submodule_object_in_another_local_worktree(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            repository = root / "local-submodule"
            repository.mkdir()
            subprocess.run(["git", "init", "-q"], cwd=repository, check=True)
            subprocess.run(["git", "config", "user.name", "Test"], cwd=repository, check=True)
            subprocess.run(["git", "config", "user.email", "test@example.com"], cwd=repository, check=True)
            (repository / "file").write_text("content", encoding="utf-8")
            subprocess.run(["git", "add", "file"], cwd=repository, check=True)
            subprocess.run(["git", "commit", "-q", "-m", "object"], cwd=repository, check=True)
            commit = subprocess.run(
                ["git", "rev-parse", "HEAD"], cwd=repository, check=True,
                text=True, stdout=subprocess.PIPE).stdout.strip()
            found = driver.find_local_submodule_source(
                [root / "missing", repository], commit)
            self.assertEqual(found, repository)

    def test_missing_local_submodule_object_returns_none(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertIsNone(driver.find_local_submodule_source(
                [Path(directory) / "missing"], "0" * 40))

    def test_local_clone_fetches_unreferenced_historical_commit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            destination = root / "destination"
            source.mkdir()
            subprocess.run(["git", "init", "-q"], cwd=source, check=True)
            subprocess.run(["git", "config", "user.name", "Test"], cwd=source, check=True)
            subprocess.run(["git", "config", "user.email", "test@example.com"], cwd=source, check=True)
            (source / "historical").write_text("old tree", encoding="utf-8")
            subprocess.run(["git", "add", "historical"], cwd=source, check=True)
            subprocess.run(["git", "commit", "-q", "-m", "historical"], cwd=source, check=True)
            historical = subprocess.run(
                ["git", "rev-parse", "HEAD"], cwd=source, check=True,
                text=True, stdout=subprocess.PIPE).stdout.strip()
            original_branch = subprocess.run(
                ["git", "branch", "--show-current"], cwd=source, check=True,
                text=True, stdout=subprocess.PIPE).stdout.strip()
            subprocess.run(["git", "checkout", "-q", "--orphan", "active"], cwd=source, check=True)
            (source / "historical").unlink()
            (source / "active").write_text("new tree", encoding="utf-8")
            subprocess.run(["git", "add", "-A"], cwd=source, check=True)
            subprocess.run(["git", "commit", "-q", "-m", "active"], cwd=source, check=True)
            subprocess.run(["git", "branch", "-D", original_branch], cwd=source, check=True,
                           stdout=subprocess.DEVNULL)

            driver.clone_local_submodule(source, destination, historical)

            checked_out = subprocess.run(
                ["git", "rev-parse", "HEAD"], cwd=destination, check=True,
                text=True, stdout=subprocess.PIPE).stdout.strip()
            self.assertEqual(checked_out, historical)
            self.assertEqual((destination / "historical").read_text(encoding="utf-8"), "old tree")


if __name__ == "__main__":
    unittest.main()
