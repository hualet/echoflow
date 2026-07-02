# EchoFlow Release Performance Gate Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore optimized packaged ASR performance and prevent every future version bump unless the candidate binary passes latency and CER checks against a compatible recorded baseline or a baseline rebuilt on the current CPU.

**Architecture:** CMake exposes one deterministic `x86-64-v3` build profile used by Debian and release benchmarks. A Python benchmark driver owns environment identity, baseline reuse, result aggregation, and evidence generation; a small deterministic verifier runs without models in CI and rejects version bumps that lack valid evidence. Existing C++ benchmark executables remain responsible for measuring ASR and VAD behavior.

**Tech Stack:** CMake 3.16, C++17, Python 3 standard library, Bash, Debian debhelper, GitHub Actions, QTest/CTest.

---

### Task 1: Add a deterministic optimized CPU build profile

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `debian/rules`
- Modify: `.github/workflows/build.yml`
- Modify: `tests/spec/run_spec.sh`

- [ ] **Step 1: Add failing build-profile spec assertions**

Add assertions requiring `ECHOFLOW_CPU_TARGET`, the x86-64-v3 GGML cache values, and Debian's explicit profile:

```bash
assert_contains "$ROOT/CMakeLists.txt" 'set(ECHOFLOW_CPU_TARGET "native" CACHE STRING' "CMake exposes the CPU target"
assert_contains "$ROOT/CMakeLists.txt" 'set(GGML_NATIVE OFF CACHE BOOL "" FORCE)' "portable target disables GGML native tuning"
assert_contains "$ROOT/CMakeLists.txt" 'set(GGML_AVX2 ON CACHE BOOL "" FORCE)' "portable target enables AVX2"
assert_contains "$ROOT/CMakeLists.txt" 'set(GGML_FMA ON CACHE BOOL "" FORCE)' "portable target enables FMA"
assert_contains "$ROOT/CMakeLists.txt" 'set(GGML_F16C ON CACHE BOOL "" FORCE)' "portable target enables F16C"
assert_contains "$ROOT/debian/rules" 'override_dh_auto_configure:' "Debian overrides CMake configuration"
assert_contains "$ROOT/debian/rules" '-- -DECHOFLOW_CPU_TARGET=x86-64-v3' "Debian selects deterministic CPU target"
```

- [ ] **Step 2: Run the spec and confirm failure**

Run: `bash tests/spec/run_spec.sh`

Expected: failures for the new CPU-target assertions.

- [ ] **Step 3: Implement the CMake profile and Debian selection**

Before `add_subdirectory(third_party/crispasr)`, define `native` and `x86-64-v3`. Reject x86-64-v3 on non-amd64 hosts and force the supported GGML switches:

```cmake
set(ECHOFLOW_CPU_TARGET "native" CACHE STRING
    "CPU target for CrispASR: native or x86-64-v3")
set_property(CACHE ECHOFLOW_CPU_TARGET PROPERTY STRINGS native x86-64-v3)

if(ECHOFLOW_CPU_TARGET STREQUAL "x86-64-v3")
    if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
        message(FATAL_ERROR "x86-64-v3 requires an amd64 build host")
    endif()
    set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
    set(GGML_AVX ON CACHE BOOL "" FORCE)
    set(GGML_AVX2 ON CACHE BOOL "" FORCE)
    set(GGML_FMA ON CACHE BOOL "" FORCE)
    set(GGML_F16C ON CACHE BOOL "" FORCE)
    set(GGML_AVX512 OFF CACHE BOOL "" FORCE)
elseif(NOT ECHOFLOW_CPU_TARGET STREQUAL "native")
    message(FATAL_ERROR "Unsupported ECHOFLOW_CPU_TARGET=${ECHOFLOW_CPU_TARGET}")
endif()
```

Use the same profile in Debian and ordinary CI:

```make
override_dh_auto_configure:
	dh_auto_configure -- -DECHOFLOW_CPU_TARGET=x86-64-v3
```

```yaml
- name: Configure
  run: cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr -DECHOFLOW_CPU_TARGET=x86-64-v3
```

- [ ] **Step 4: Verify configuration and specs**

Run:

```bash
cmake -S . -B /tmp/echoflow-v3-check -DCMAKE_BUILD_TYPE=RelWithDebInfo -DECHOFLOW_CPU_TARGET=x86-64-v3
cmake -LA -N /tmp/echoflow-v3-check | grep -E 'GGML_(NATIVE|AVX2|FMA|F16C)'
bash tests/spec/run_spec.sh
```

Expected: `GGML_NATIVE=OFF`, the three required features are `ON`, and all specs pass.

- [ ] **Step 5: Commit the build profile**

```bash
git add CMakeLists.txt debian/rules .github/workflows/build.yml tests/spec/run_spec.sh
git commit -m "perf(build): use deterministic optimized CPU target" -m "Keep local native builds available while making release and CI builds use the measured x86-64-v3 GGML profile."
```

### Task 2: Make benchmark output complete and stable

**Files:**
- Create: `tests/benchmarks/BenchmarkManifest.h`
- Create: `tests/benchmarks/BenchmarkManifest.cpp`
- Modify: `tests/benchmarks/voice_latency_benchmark.cpp`
- Modify: `tests/benchmarks/vad_replay_benchmark.cpp`
- Modify: `tests/data/vad/manifest.example.json`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/test_benchmark_manifest.cpp`

- [ ] **Step 1: Write a failing manifest/JSON contract test**

Add a QTest that loads a temporary manifest containing `id`, `condition`, `audio`, `sha256`, and `reference`, then verifies missing IDs and duplicate IDs are rejected. Keep parsing in a focused `BenchmarkManifest` helper declared in the benchmark source support library.

```cpp
void TestBenchmarkManifest::rejectsDuplicateIds()
{
    QTemporaryFile file;
    QVERIFY(file.open());
    file.write(R"([{"id":"normal","audio":"a.wav","sha256":"00","reference":"甲"},{"id":"normal","audio":"b.wav","sha256":"11","reference":"乙"}])");
    file.flush();
    QVERIFY_EXCEPTION_THROWN(echoflow::loadBenchmarkManifest(file.fileName()), std::runtime_error);
}
```

- [ ] **Step 2: Run the focused test and confirm failure**

Run: `cmake --build build --target test_benchmark_manifest && ./build/tests/test_benchmark_manifest`

Expected: build failure because `BenchmarkManifest` does not exist.

- [ ] **Step 3: Add the manifest helper and stable fields**

Create `tests/benchmarks/BenchmarkManifest.{h,cpp}` with `BenchmarkEntry { id, audio, sha256, reference, condition, speech }`, path resolution, unique-ID validation, and SHA field validation. Update both benchmark binaries to emit `id`, `transcript`, and `reference_chars`; keep one compact JSON object per line. Update the example manifest to this schema:

```json
[
  {
    "id": "normal-dictation",
    "condition": "normal",
    "audio": "/absolute/path/to/normal.wav",
    "sha256": "replace-with-64-lowercase-hex-digits",
    "reference": "参考转写文本",
    "speech": [[0.2, 3.8]]
  }
]
```

- [ ] **Step 4: Run focused and existing tests**

Run:

```bash
cmake --build build --target test_benchmark_manifest voice_latency_benchmark vad_replay_benchmark
./build/tests/test_benchmark_manifest
ctest --test-dir build --output-on-failure
```

Expected: manifest test and all existing tests pass.

- [ ] **Step 5: Commit the benchmark contract**

```bash
git add tests/benchmarks tests/data/vad/manifest.example.json tests/CMakeLists.txt tests/test_benchmark_manifest.cpp
git commit -m "test(performance): stabilize benchmark result schema" -m "Give every sample a verified identity and include transcripts so release evidence can compare latency and CER deterministically."
```

### Task 3: Implement pure comparison and identity logic

**Files:**
- Create: `scripts/performance_gate.py`
- Create: `tests/test_performance_gate.py`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing Python unit tests**

Cover compatible identity, CPU mismatch, aggregate 10% regression, per-sample 20% regression, CER regression, empty transcript, and missing sample:

```python
class PerformanceGateTest(unittest.TestCase):
    def test_reuses_matching_identity(self):
        self.assertTrue(gate.identities_match(IDENTITY, dict(IDENTITY)))

    def test_cpu_mismatch_requires_rebuild(self):
        other = dict(IDENTITY, cpu_model="Different CPU")
        self.assertFalse(gate.identities_match(IDENTITY, other))

    def test_aggregate_latency_regression_fails(self):
        result = gate.compare_results(
            baseline_samples([100.0, 100.0]),
            candidate_samples([111.0, 111.0]))
        self.assertFalse(result["passed"])
        self.assertIn("aggregate_latency", result["failures"])
```

- [ ] **Step 2: Run tests and confirm import failure**

Run: `python3 -m unittest -v tests.test_performance_gate`

Expected: failure importing `scripts.performance_gate`.

- [ ] **Step 3: Implement deterministic comparison functions**

Implement standard-library-only functions:

```python
IDENTITY_FIELDS = (
    "cpu_model", "architecture", "cpu_flags", "build_profile",
    "benchmark_schema", "model_sha256", "manifest_sha256",
    "config_sha256", "threads", "openblas_threads",
)

def identities_match(left, right):
    return all(left.get(key) == right.get(key) for key in IDENTITY_FIELDS)

def compare_results(baseline, candidate):
    baseline_by_id = {sample["id"]: sample for sample in baseline["samples"]}
    candidate_by_id = {sample["id"]: sample for sample in candidate["samples"]}
    failures = []
    if baseline_by_id.keys() != candidate_by_id.keys():
        failures.append("sample_set")
    ratios = {}
    baseline_medians = []
    candidate_medians = []
    for sample_id in sorted(baseline_by_id.keys() & candidate_by_id.keys()):
        before = statistics.median(baseline_by_id[sample_id]["latency_ms"])
        after = statistics.median(candidate_by_id[sample_id]["latency_ms"])
        baseline_medians.append(before)
        candidate_medians.append(after)
        ratios[sample_id] = after / before
        if ratios[sample_id] > 1.20:
            failures.append(f"sample_latency:{sample_id}")
        if candidate_by_id[sample_id]["required"] and not candidate_by_id[sample_id]["transcript"].strip():
            failures.append(f"empty_transcript:{sample_id}")
    aggregate_ratio = statistics.median(candidate_medians) / statistics.median(baseline_medians)
    if aggregate_ratio > 1.10:
        failures.append("aggregate_latency")
    if candidate["aggregate_cer"] > baseline["aggregate_cer"]:
        failures.append("aggregate_cer")
    metrics = {"aggregate_ratio": aggregate_ratio, "sample_ratios": ratios}
    return {"passed": not failures, "failures": failures, "metrics": metrics}
```

The CLI accepts `compare BASELINE.json CANDIDATE.json --output EVIDENCE.json` and exits 0 only when the comparison passes.

- [ ] **Step 4: Run unit tests through CTest**

Add:

```cmake
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME performance_gate_tests
    COMMAND ${Python3_EXECUTABLE} -m unittest -v tests.test_performance_gate)
set_tests_properties(performance_gate_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
```

Run: `python3 -m unittest -v tests.test_performance_gate && ctest --test-dir build -R performance_gate_tests --output-on-failure`

Expected: all comparison cases pass.

- [ ] **Step 5: Commit comparison logic**

```bash
git add scripts/performance_gate.py tests/test_performance_gate.py tests/CMakeLists.txt
git commit -m "feat(performance): compare release benchmark results" -m "Reject latency, CER, transcript, sample, and identity regressions with model-free deterministic checks."
```

### Task 4: Automate baseline reuse and same-machine dual builds

**Files:**
- Create: `scripts/release_performance.py`
- Create: `scripts/check-release-performance.py`
- Create: `tests/test_release_performance_driver.py`
- Modify: `README.md`

- [ ] **Step 1: Write failing driver decision tests**

Mock command execution and filesystem operations. Verify matching identity schedules only the candidate build, mismatched CPU schedules baseline and candidate builds, fewer than three iterations fails, and unreachable submodules fail:

```python
def test_cpu_mismatch_builds_both_revisions(self):
    plan = driver.make_build_plan(
        baseline_identity={**IDENTITY, "cpu_model": "old"},
        current_identity=IDENTITY,
        baseline_ref="v0.2.1", candidate_ref="HEAD")
    self.assertEqual([item.ref for item in plan], ["v0.2.1", "HEAD"])
```

- [ ] **Step 2: Run tests and confirm import failure**

Run: `python3 -m unittest -v tests.test_release_performance_driver`

Expected: failure importing `scripts.release_performance`.

- [ ] **Step 3: Implement the release benchmark driver**

`release_performance.py` owns the importable logic and
`check-release-performance.py` is a thin executable entry point. The driver
must:

```text
1. Refuse a dirty tree and iterations below 3.
2. Read normalized CPU model, architecture, and sorted required flags.
3. Hash config, model, manifest, and every WAV before running.
4. Reuse the previous JSON only when performance_gate.identities_match is true.
5. Otherwise create temporary detached worktrees for baseline and candidate.
6. Run git submodule update --init --recursive third_party/crispasr in each.
7. Configure RelWithDebInfo with ECHOFLOW_CPU_TARGET=x86-64-v3.
8. Build benchmark targets twice and fail if the second build compiles C/C++.
9. Run at least three warm iterations and VAD replay for every manifest sample.
10. Write raw result JSON, call compare_results, and always write evidence.
```

Expose exact arguments:

```text
scripts/check-release-performance.py --baseline-ref v0.2.1 --candidate-ref HEAD
  --version 0.2.2 --config FILE --model FILE --manifest FILE
  --iterations 3 --previous-evidence FILE --output FILE
```

- [ ] **Step 4: Test the driver without models**

Run: `python3 -m unittest -v tests.test_release_performance_driver`

Expected: all mocked orchestration and failure-path tests pass.

- [ ] **Step 5: Document the local command and commit**

Add the exact command above to README, explain baseline reuse and automatic dual build, then commit:

```bash
git add scripts/release_performance.py scripts/check-release-performance.py tests/test_release_performance_driver.py README.md
git commit -m "feat(performance): automate release baseline checks" -m "Reuse compatible same-CPU evidence and rebuild both revisions when benchmark identity differs."
```

### Task 5: Enforce evidence on every version bump

**Files:**
- Create: `scripts/verify-release-performance-evidence.py`
- Create: `tests/test_release_evidence.py`
- Modify: `.github/workflows/build.yml`
- Modify: `.github/workflows/deb.yml`
- Modify: `tests/spec/run_spec.sh`
- Modify: `AGENTS.md`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing evidence-verifier tests**

Use temporary Git repositories to test valid two-step release commits and reject missing evidence, wrong candidate parent, failed status, wrong previous tag, source changes in the release commit, incompatible reused CPU identity, and invalid threshold math.

```python
def test_release_commit_source_change_is_rejected(self):
    repo = self.make_release_repo(release_files={"service/main.cpp": "changed"})
    result = self.verify(repo)
    self.assertNotEqual(result.returncode, 0)
    self.assertIn("release commit changes disallowed file", result.stderr)
```

- [ ] **Step 2: Run tests and confirm failure**

Run: `python3 -m unittest -v tests.test_release_evidence`

Expected: verifier file is missing.

- [ ] **Step 3: Implement the model-free verifier**

The verifier reads `debian/changelog`, parent changelog, Git diff, tag, and `docs/performance/releases/<version>.json`. It requires `status=pass`, candidate commit equal to first parent, the expected prior tag, internally valid medians/thresholds, compatible reused identity or `baseline_mode=rebuilt`, and only these release files:

```python
ALLOWED_RELEASE_PATHS = {
    "debian/changelog",
    f"docs/performance/releases/{version}.json",
}
```

Support `--commit HEAD` and `--tag vX.Y.Z`; print one precise error per failed invariant and exit non-zero.

- [ ] **Step 4: Wire verifier into tests and workflows**

Add the Python test to CTest. In `build.yml`, run the verifier only when the changelog version differs from the first parent. In `deb.yml`, always run:

```yaml
- name: Verify release performance evidence
  run: python3 scripts/verify-release-performance-evidence.py --commit HEAD --tag "${RELEASE_TAG}"
```

Update `AGENTS.md` Version Bumping to require the code commit, benchmark evidence, release-only commit, then tag.

- [ ] **Step 5: Run verifier tests and specs**

Run:

```bash
python3 -m unittest -v tests.test_release_evidence
bash tests/spec/run_spec.sh
ctest --test-dir build --output-on-failure
```

Expected: all tests pass and a fixture version bump without evidence fails.

- [ ] **Step 6: Commit release enforcement**

```bash
git add scripts/verify-release-performance-evidence.py tests/test_release_evidence.py .github/workflows/build.yml .github/workflows/deb.yml tests/spec/run_spec.sh AGENTS.md tests/CMakeLists.txt
git commit -m "ci(release): require performance evidence" -m "Block version bumps and release tags unless their parent candidate passed a compatible latency and CER baseline."
```

### Task 6: Measure the fix and validate development and packaging paths

**Files:**
- Create: `docs/performance/releases/0.2.2.json`
- Modify: `docs/performance/voice-latency-optimization-report.md`

- [ ] **Step 1: Resolve the real local benchmark inputs**

Verify the model, config, and fixed recording manifest exist and hash them:

```bash
sha256sum "$MODEL" "$CONFIG" "$MANIFEST"
python3 -c 'import json,sys; data=json.load(open(sys.argv[1])); assert len(data) >= 5' "$MANIFEST"
```

Expected: all inputs exist and the manifest covers normal, pauses, quiet, short, and long speech.

- [ ] **Step 2: Run the release comparison**

Run:

```bash
python3 scripts/check-release-performance.py \
  --baseline-ref v0.2.1 --candidate-ref HEAD --version 0.2.2 \
  --config "$CONFIG" --model "$MODEL" --manifest "$MANIFEST" \
  --iterations 3 --output docs/performance/releases/0.2.2.json
```

Expected: either compatible history is reused or both revisions are built on this CPU; aggregate latency is within 110%, each sample within 120%, CER does not increase, and every required transcript is non-empty.

- [ ] **Step 3: Verify no-op local development rebuild**

Run:

```bash
./install-user.sh --no-start
cmake --build build --verbose | tee /tmp/echoflow-second-build.log
! grep -E ' -c .*\.(c|cc|cpp) ' /tmp/echoflow-second-build.log
./build/service/echoflow-service --self-test
```

Expected: second build compiles no C/C++ source and self-test succeeds.

- [ ] **Step 4: Build and inspect the Debian package**

Run:

```bash
dpkg-buildpackage -us -uc -b
PACKAGE="$(find .. -maxdepth 1 -name 'echoflow_*_amd64.deb' -print -quit)"
test -n "$PACKAGE"
dpkg-deb -x "$PACKAGE" /tmp/echoflow-package-check
test -x /tmp/echoflow-package-check/usr/bin/echoflow-service
grep -F 'Library=libechoflow' /tmp/echoflow-package-check/usr/share/fcitx5/addon/echoflow.conf
```

Expected: package builds, contains the service, and the Fcitx addon references the packaged module.

- [ ] **Step 5: Run the complete verification set**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
./build/service/echoflow-service --print-default-config
./build/service/echoflow-service --self-test
bash -n install-user.sh uninstall-user.sh tests/spec/*.sh
sh -n run.sh
git diff --check
```

Expected: all commands succeed.

- [ ] **Step 6: Record findings and commit candidate code**

Update the report with exact CPU identity, commands, hashes, raw medians, CER, whether the baseline was reused or rebuilt, the old Debian slowdown, candidate results, and any negative findings. Commit the report and evidence only after the candidate implementation commits are final:

```bash
git add docs/performance/voice-latency-optimization-report.md docs/performance/releases/0.2.2.json
git commit -m "docs(performance): record release baseline" -m "Capture same-machine latency and recognition-quality evidence for the optimized candidate build."
```

Do not modify `debian/changelog` or create a release tag in this task. A later version bump must use the two-step release-only commit required by the verifier.
