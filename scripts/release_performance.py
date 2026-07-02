#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later

"""Release benchmark orchestration for EchoFlow."""

import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path

from scripts.performance_gate import compare_results, identities_match


@dataclass(frozen=True)
class BuildPlanEntry:
    role: str
    ref: str


def validate_iterations(iterations):
    if iterations < 3:
        raise ValueError("release benchmarks require at least 3 iterations")
    return iterations


def make_build_plan(*, baseline_identity, current_identity, baseline_ref, candidate_ref):
    plan = []
    if baseline_identity is None or not identities_match(baseline_identity, current_identity):
        plan.append(BuildPlanEntry("baseline", baseline_ref))
    plan.append(BuildPlanEntry("candidate", candidate_ref))
    return plan


def sha256_file(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_path(path):
    path = Path(path).resolve()
    if path.is_file():
        return sha256_file(path)
    if not path.is_dir():
        raise ValueError(f"model path does not exist: {path}")
    digest = hashlib.sha256()
    for child in sorted(item for item in path.rglob("*") if item.is_file()):
        digest.update(child.relative_to(path).as_posix().encode())
        digest.update(b"\0")
        digest.update(sha256_file(child).encode())
        digest.update(b"\0")
    return digest.hexdigest()


def load_manifest(path):
    manifest_path = Path(path).resolve()
    with manifest_path.open(encoding="utf-8") as stream:
        entries = json.load(stream)
    if not isinstance(entries, list) or not entries:
        raise ValueError("manifest must be a non-empty JSON array")
    ids = set()
    normalized = []
    for raw in entries:
        sample = dict(raw)
        sample_id = sample.get("id", "")
        if not sample_id or sample_id in ids:
            raise ValueError(f"manifest IDs must be present and unique: {sample_id}")
        audio = Path(sample.get("audio", ""))
        if not audio.is_absolute():
            audio = manifest_path.parent / audio
        audio = audio.resolve()
        if not audio.is_file():
            raise ValueError(f"missing audio: {sample_id}: {audio}")
        actual_hash = sha256_file(audio)
        if sample.get("sha256") != actual_hash:
            raise ValueError(f"sha256 mismatch: {sample_id}")
        sample["audio"] = str(audio)
        sample["sha256"] = actual_hash
        sample["required"] = sample.get("required", True)
        ids.add(sample_id)
        normalized.append(sample)
    return normalized


def verify_noop_build_output(output):
    if re.search(r"Building (?:C|CXX) object", output):
        raise RuntimeError("incremental build compiled C/C++ source")


def _run(command, *, cwd, capture=True):
    command = [str(item) for item in command]
    result = subprocess.run(
        command,
        cwd=cwd,
        check=False,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.STDOUT if capture else None,
    )
    if result.returncode != 0:
        output = result.stdout or ""
        raise RuntimeError(f"command failed ({result.returncode}): {' '.join(command)}\n{output}")
    return result.stdout or ""


def _git_root():
    return Path(_run(["git", "rev-parse", "--show-toplevel"], cwd=Path.cwd()).strip())


def _git_resolve(root, ref):
    return _run(["git", "rev-parse", f"{ref}^{{commit}}"], cwd=root).strip()


def find_local_submodule_source(candidates, commit):
    for candidate in candidates:
        candidate = Path(candidate)
        if not candidate.is_dir():
            continue
        result = subprocess.run(
            ["git", "cat-file", "-e", f"{commit}^{{commit}}"],
            cwd=candidate,
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        if result.returncode == 0:
            return candidate
    return None


def clone_local_submodule(source, destination, commit):
    _run(["git", "clone", "--shared", "--no-checkout", str(source), str(destination)],
         cwd=destination.parent)
    _run(["git", "update-ref", "refs/echoflow/historical-baseline", commit],
         cwd=destination)
    _run(["git", "checkout", "--detach", commit], cwd=destination)


def _local_submodule_candidates(root):
    output = _run(["git", "worktree", "list", "--porcelain"], cwd=root)
    return [
        Path(line.removeprefix("worktree ")) / "third_party" / "crispasr"
        for line in output.splitlines()
        if line.startswith("worktree ")
    ]


def _initialize_submodule(root, tree, ref):
    listing = _run(["git", "ls-tree", ref, "third_party/crispasr"], cwd=root).strip()
    fields = listing.split()
    if len(fields) < 3 or fields[1] != "commit":
        raise RuntimeError(f"cannot resolve CrispASR gitlink for {ref}")
    commit = fields[2]
    source = find_local_submodule_source(_local_submodule_candidates(root), commit)
    destination = tree / "third_party" / "crispasr"
    if source is None:
        _run(["git", "submodule", "update", "--init", "--recursive",
              "third_party/crispasr"], cwd=tree)
        return
    if destination.exists():
        shutil.rmtree(destination)
    clone_local_submodule(source, destination, commit)


def _cpu_fields():
    model = platform.processor().strip()
    flags = []
    cpuinfo = Path("/proc/cpuinfo")
    if cpuinfo.exists():
        for line in cpuinfo.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("model name") and not model:
                model = line.split(":", 1)[1].strip()
            if line.startswith("flags"):
                flags = line.split(":", 1)[1].split()
                break
    required = sorted(set(flags) & {"avx", "avx2", "bmi2", "f16c", "fma", "sse4_2"})
    return model or "unknown", required


def make_identity(*, model, config, manifest, threads, openblas_threads):
    cpu_model, cpu_flags = _cpu_fields()
    return {
        "cpu_model": cpu_model,
        "architecture": platform.machine(),
        "cpu_flags": cpu_flags,
        "build_profile": "x86-64-v3",
        "benchmark_schema": 1,
        "model_sha256": sha256_path(model),
        "manifest_sha256": sha256_file(manifest),
        "config_sha256": sha256_file(config),
        "threads": threads,
        "openblas_threads": openblas_threads,
    }


def _json_lines(output):
    values = []
    for line in output.splitlines():
        line = line.strip()
        if line.startswith("{"):
            values.append(json.loads(line))
    return values


def _edit_distance(left, right):
    previous = list(range(len(right) + 1))
    for index, left_char in enumerate(left, 1):
        current = [index]
        for offset, right_char in enumerate(right, 1):
            current.append(min(
                previous[offset] + 1,
                current[offset - 1] + 1,
                previous[offset - 1] + (left_char != right_char),
            ))
        previous = current
    return previous[-1]


def _configure_and_build(tree, build):
    cmake_options = [
        "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
        "-DECHOFLOW_CPU_TARGET=x86-64-v3",
        "-DGGML_NATIVE=OFF",
        "-DGGML_SSE42=ON",
        "-DGGML_AVX=ON",
        "-DGGML_AVX2=ON",
        "-DGGML_BMI2=ON",
        "-DGGML_FMA=ON",
        "-DGGML_F16C=ON",
        "-DGGML_AVX512=OFF",
    ]
    _run(["cmake", "-S", ".", "-B", str(build), *cmake_options], cwd=tree)
    targets = ["voice_latency_benchmark", "vad_replay_benchmark"]
    _run(["cmake", "--build", str(build), "--parallel", str(os.cpu_count() or 1),
          "--target", *targets], cwd=tree)
    second = _run(["cmake", "--build", str(build), "--target", *targets], cwd=tree)
    verify_noop_build_output(second)


def _benchmark_build(build, *, config, manifest_path, entries, iterations, threads,
                     openblas_threads):
    voice = build / "tests" / "voice_latency_benchmark"
    vad = build / "tests" / "vad_replay_benchmark"
    samples = []
    voice_transcripts = {}
    for entry in entries:
        output = _run([
            str(voice), "--transcribe-file", entry["audio"], "--config", str(config),
            "--threads", str(threads), "--openblas-threads", str(openblas_threads),
            "--iterations", str(iterations + 1),
        ], cwd=build)
        rows = [row for row in _json_lines(output) if row.get("mode") == "transcribe-file"]
        if len(rows) != iterations + 1:
            raise RuntimeError(f"unexpected ASR iteration count: {entry['id']}")
        measured = rows[1:]
        transcript = measured[-1].get("transcript", "")
        voice_transcripts[entry["id"]] = transcript
        samples.append({
            "id": entry["id"],
            "sha256": entry["sha256"],
            "condition": entry.get("condition", ""),
            "required": entry.get("required", True),
            "latency_ms": [row["transcribe_ms"] for row in measured],
            "transcript": transcript,
            "reference": entry.get("reference", ""),
        })

    vad_rows = _json_lines(_run([
        str(vad), "--manifest", str(manifest_path), "--backend", "energy",
        "--config", str(config),
    ], cwd=build))
    clips = [row for row in vad_rows if row.get("type") == "clip"]
    clips_by_id = {row.get("id"): row for row in clips if row.get("id")}
    clips_by_audio = {str(Path(row["audio"]).resolve()): row for row in clips}
    total_errors = 0
    total_reference = 0
    for sample, entry in zip(samples, entries):
        clip = clips_by_id.get(entry["id"]) or clips_by_audio.get(entry["audio"], {})
        if not sample["transcript"]:
            sample["transcript"] = clip.get("transcript", "")
        reference = entry.get("reference", "")
        errors = _edit_distance(reference, sample["transcript"]) if reference else 0
        sample["cer"] = errors / len(reference) if reference else 0.0
        total_errors += errors
        total_reference += len(reference)
    return {
        "samples": samples,
        "aggregate_cer": total_errors / total_reference if total_reference else 0.0,
        "vad": vad_rows,
    }


def _previous_baseline(path, baseline_ref):
    if path is None:
        return None, None
    with Path(path).open(encoding="utf-8") as stream:
        evidence = json.load(stream)
    if evidence.get("version") != baseline_ref.removeprefix("v"):
        return None, None
    candidate = evidence.get("candidate", {})
    return candidate.get("identity"), candidate.get("result")


def run_release_check(args):
    iterations = validate_iterations(args.iterations)
    root = _git_root()
    status = _run(["git", "status", "--porcelain", "--untracked-files=no"], cwd=root)
    if status.strip():
        raise RuntimeError("release benchmark requires a clean tracked worktree")
    entries = load_manifest(args.manifest)
    current_identity = make_identity(
        model=args.model,
        config=args.config,
        manifest=args.manifest,
        threads=args.threads,
        openblas_threads=args.openblas_threads,
    )
    baseline_identity, reused_result = _previous_baseline(args.previous_evidence, args.baseline_ref)
    plan = make_build_plan(
        baseline_identity=baseline_identity,
        current_identity=current_identity,
        baseline_ref=args.baseline_ref,
        candidate_ref=args.candidate_ref,
    )
    commits = {
        "baseline": _git_resolve(root, args.baseline_ref),
        "candidate": _git_resolve(root, args.candidate_ref),
    }
    results = {"baseline": reused_result} if reused_result is not None else {}
    temporary_root = Path(tempfile.mkdtemp(prefix="echoflow-release-performance-"))
    worktrees = []
    try:
        for item in plan:
            tree = temporary_root / item.role
            build = temporary_root / f"{item.role}-build"
            _run(["git", "worktree", "add", "--detach", str(tree), item.ref], cwd=root)
            worktrees.append(tree)
            _initialize_submodule(root, tree, item.ref)
            _configure_and_build(tree, build)
            result = _benchmark_build(
                build,
                config=Path(args.config).resolve(),
                manifest_path=Path(args.manifest).resolve(),
                entries=entries,
                iterations=iterations,
                threads=args.threads,
                openblas_threads=args.openblas_threads,
            )
            result["identity"] = current_identity
            result["commit"] = commits[item.role]
            results[item.role] = result
        if results.get("baseline") is None:
            raise RuntimeError("baseline result is unavailable")
        comparison = compare_results(results["baseline"], results["candidate"])
        evidence = {
            "schema_version": 1,
            "status": "pass" if comparison["passed"] else "fail",
            "version": args.version,
            "baseline": {
                "ref": args.baseline_ref,
                "commit": commits["baseline"],
                "mode": "reused" if reused_result is not None else "rebuilt",
                "identity": baseline_identity if reused_result is not None else current_identity,
                "result": results["baseline"],
            },
            "candidate": {
                "ref": args.candidate_ref,
                "commit": commits["candidate"],
                "identity": current_identity,
                "result": results["candidate"],
            },
            "comparison": comparison,
        }
        output = Path(args.output)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(evidence, ensure_ascii=False, indent=2) + "\n",
                          encoding="utf-8")
        if not comparison["passed"]:
            raise RuntimeError("performance gate failed: " + ", ".join(comparison["failures"]))
        return evidence
    finally:
        for tree in reversed(worktrees):
            subprocess.run(["git", "worktree", "remove", "--force", str(tree)],
                           cwd=root, check=False)
        shutil.rmtree(temporary_root, ignore_errors=True)
