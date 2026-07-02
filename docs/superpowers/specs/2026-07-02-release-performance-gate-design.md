# EchoFlow Release Performance Gate Design

Date: 2026-07-02
Status: proposed

## Problem

EchoFlow 0.2.1 exposed two release failures that ordinary build and unit tests
did not detect:

- the Debian artifact decoded the same 9.5-second recording in about 11.7
  seconds, while a local optimized build required about 3.1-3.3 seconds;
- a version could be bumped and tagged without comparing ASR latency and
  transcript quality against the previous release on the same machine.

The release process must restore packaged performance and make a fresh,
machine-local performance comparison a hard prerequisite for every version
bump.

## Goals

- Make the amd64 Debian build use a deterministic optimized CPU target instead
  of inheriting an unknown CI host's native or generic target.
- Compare the candidate binary with the previous accepted binary baseline on
  equivalent hardware and fixed benchmark inputs.
- Reject a release if median ASR latency exceeds 110% of the baseline or if
  aggregate CER is higher than the baseline.
- Reject empty transcripts, missing samples, mismatched models, and stale or
  unrelated performance evidence.
- Preserve no-op incremental builds and the existing 14-test repository suite.

## Non-goals

- Download model weights in ordinary pull-request CI.
- Claim cross-machine latency comparability.
- Replace the current ASR engine or VAD design.
- Treat synthetic state-machine timings as an ASR performance gate.

## Packaged Performance Fix

The amd64 package will disable `GGML_NATIVE` and explicitly enable the
x86-64-v3 feature set used by GGML: SSE4.2, F16C, FMA, BMI2, AVX, and AVX2.
This produces a reproducible package independent of the CI runner's exposed CPU
flags. The supported Debian/deepin amd64 target is therefore x86-64-v3.

Current evidence on the same AMD Ryzen 7 6800H and the same 9.5-second Mandarin
recording is:

| Build | Median latency | Transcript bytes |
| --- | ---: | ---: |
| Debian 0.2.1 artifact | about 11.7 s | non-empty |
| local `-march=native` | 3.25 s | 93 |
| explicit x86-64-v3 | 2.66 s | 93 |

The final implementation benchmarks the optimized CMake binary. Debian package
validation separately proves that packaging uses the same deterministic CPU
feature configuration; package-to-package performance comparison is not part of
the release gate.

## Benchmark Inputs

A release benchmark manifest lists repository-independent WAV paths and exact
reference transcripts. Each sample entry includes a stable ID, condition, WAV
SHA-256, and reference text. The gate records the model file SHA-256 and rejects
comparison files that use different model or sample hashes.

The default release set must cover:

- normal Mandarin dictation;
- natural pauses and multiple VAD segments;
- quiet speech;
- a short interjection;
- a 30-second-or-longer dictation or deterministic repeated long fixture.

Private recordings and model weights remain outside Git. A committed example
manifest documents the schema; the local release configuration points at the
real manifest.

## Same-machine Comparison

`scripts/check-release-performance.sh` accepts a baseline tag, candidate commit,
config path, model path, manifest, iteration count, and output path. It first
checks the latest accepted baseline record. Historical baseline data is reused
only when all comparison-critical identity fields match:

- normalized CPU model and architecture;
- required CPU feature flags;
- benchmark schema and build profile;
- model, manifest, sample, configuration, and thread-setting hashes.

If every field matches, the script builds only the candidate binary and compares
it with the recorded baseline metrics. If the CPU or any other identity field
does not match, it creates isolated worktrees for the baseline tag and candidate
commit, initializes both reachable submodules, and builds both binaries on the
current machine with the same compiler and x86-64-v3 configuration. An
unbuildable baseline is a hard error because its measurements cannot be validly
substituted from another machine.

For each measured binary it runs:

- at least three warm ASR iterations per non-empty sample;
- the VAD replay benchmark for segmentation, first-stable latency, stop latency,
  transcript, and CER.

For each source tree built during the check, it additionally runs a second build
in the same directory to prove the incremental build is a no-op.

The comparator uses per-sample medians and an aggregate median. The release
passes only when:

- candidate aggregate median latency <= baseline aggregate median * 1.10;
- no candidate sample median exceeds its baseline by more than 20%;
- candidate aggregate CER <= baseline aggregate CER;
- every sample expected to contain speech produces non-empty text;
- all expected samples are present in both result sets;
- the candidate binary satisfies the threshold against either the compatible
  recorded baseline or the freshly rebuilt baseline binary.

The 20% per-sample guard prevents a large regression in one condition from being
hidden by a faster aggregate while allowing normal short-sample variance.

## Evidence Format

The check writes `docs/performance/releases/<version>.json` with:

- schema version and pass/fail status;
- baseline source version, release tag, resolved commit, and binary SHA-256;
- tested candidate commit;
- CPU model, architecture flags, compiler, build type, and thread settings;
- model ID and SHA-256;
- manifest SHA-256 and every WAV SHA-256;
- raw per-iteration latency, median latency, transcript, and CER;
- aggregate baseline/candidate metrics and threshold calculations;
- candidate binary SHA-256 and benchmark metrics;
- whether the baseline was reused or rebuilt, including every identity field
  used to make that decision.

Raw transcript text is included because CER evidence must be auditable. The
manifest controls whether a recording is appropriate to store in release
evidence.

## Version-bump Enforcement

`scripts/verify-release-performance-evidence.py` is deterministic and requires
no model. It runs in normal CI and checks any commit that changes the first
version in `debian/changelog`, plus every `v*` tag.

The release commit structure is intentionally two-step:

1. Commit all candidate code and run the performance comparison on that commit.
2. Create a release-only commit containing the changelog/version update and the
   generated evidence JSON.

The verifier requires:

- evidence `status` is `pass`;
- evidence candidate commit equals the release commit's first parent;
- the evidence version equals the new changelog/tag version;
- the release commit changes only allowed release metadata and evidence files;
- latency and CER calculations satisfy the configured thresholds;
- the baseline tag matches the previous changelog version;
- a reused baseline has matching CPU and benchmark identity fields, or a
  mismatched baseline was rebuilt on the candidate machine;
- the manifest/model/sample hashes are complete and internally consistent.

The first gated release has one explicit bootstrap exception because 0.2.1 has
no committed baseline record. It must build both the 0.2.1 tag and candidate on
the current machine and record both result sets. Every later release may reuse
the previous accepted metrics only under the identity checks above.

This avoids a circular commit hash while ensuring no unbenchmarked source change
can be smuggled into the version-bump commit. The build and Debian workflows run
the verifier before compiling or publishing a release.

## Failure Handling

The benchmark script exits non-zero and prints the exact failed metric, sample,
baseline value, candidate value, and threshold. It still writes a failed JSON
report so negative results remain inspectable, but failed evidence cannot
authorize a version bump.

Missing model files, recordings, dirty worktrees, baseline or candidate
submodule commits, mismatched hashes, fewer than three iterations, or failed
baseline builds are hard errors rather than skipped checks.

## Testing

- Unit-test JSON comparison with pass, latency regression, per-sample outlier,
  CER regression, empty transcript, hash mismatch, and missing sample fixtures.
- Shell/spec-test version-bump detection and allowed-file enforcement using
  temporary Git repositories.
- Run the existing CTest suite and shell specifications.
- Build twice and assert the second build compiles no C/C++ source.
- Build a real Debian package and inspect its build metadata, Fcitx addon, and
  binary to confirm it uses the tested deterministic feature configuration.
  Keep a separate manual install smoke test for service and addon integration.
- Record the reused or rebuilt baseline, native diagnostic build, and candidate
  x86-64-v3 results in the performance report.

## Acceptance Criteria

- The candidate binary passes the 110% aggregate and 20% per-sample latency
  guards against a compatible recorded baseline or a baseline rebuilt on the
  same machine.
- Aggregate CER does not increase and all required transcripts are non-empty.
- A version bump without fresh valid evidence fails locally and in CI.
- A release commit containing source code in addition to release metadata fails.
- The final report contains exact commands, hashes, raw metrics, negative
  findings, and the accepted conclusion.
