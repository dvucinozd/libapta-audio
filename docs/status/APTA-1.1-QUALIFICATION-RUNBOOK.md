# APTA 1.1 qualification runbook

This runbook defines the reproducible path from a private manually labelled DJ
audio corpus to the frozen APTA 1.1 qualification reports. It does not provide
acceptance evidence by itself.

## Evidence boundary

- Real audio remains local and is never committed to the repository.
- The local staging CSV may contain private filenames or relative paths.
- Frozen labels use only opaque IDs derived from full audio SHA-256 digests.
- The corpus runner re-hashes every source and requires an exact match with the
  frozen manifest before analysis starts.
- `apta-analyze` is invoked with an anonymous input name of the form
  `track-<hash>.wav`, so the original filename/path cannot enter `.apta`
  application-source metadata.
- Runner mapping and run metadata contain only relative output paths, opaque
  track IDs, exact source revision and SHA-256 digests.
- Synthetic/undersized runs are diagnostic-only and cannot close an acceptance
  blocker.

## Prerequisites

Build the desktop analyzer and inspector from the exact source revision that is
to be qualified:

```sh
cmake -S . -B build-qual \
  -DAPTA_BUILD_TESTS=ON \
  -DAPTA_WARNINGS_AS_ERRORS=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-qual --parallel 2 --target apta_analyze apta_inspect
REV=$(git rev-parse HEAD)
```

`REV` must be the full 40-character commit SHA. Do not qualify a dirty or
uncommitted build as release evidence.

## 1. Prepare the private staging corpus

For final DJ acceptance, use at least 48 genuinely fresh, manually verified
tracks that were not used to tune the candidate. Keep the audio outside the
repository if desired.

The private staging CSV schema is:

```text
source,key_tonic,key_mode,meter_numerator,meter_denominator,downbeat_frame,beat_period_frames
```

`source` is resolved relative to `--corpus-root`. Current final acceptance
labels permit major/minor keys and 3/4 or 4/4 meter. Frame labels must refer to
the exact WAV files being analyzed.

## 2. Freeze the corpus

```sh
python3 tools/apta_1_1_freeze_corpus.py \
  --corpus-root /private/apta-corpus \
  --staging-labels /private/apta-staging.csv \
  --labels-output qualification/labels.csv \
  --manifest-output qualification/manifest.json \
  --frozen-utc 2026-01-01T00:00:00Z \
  --reference-source manually-verified-fresh-corpus \
  --verification-procedure independent-dj-label-review
```

Do not use `--allow-diagnostic` for final acceptance. The freezer rejects fewer
than 48 tracks, duplicate audio content, invalid labels, missing files and path
escape from the corpus root.

## 3. Analyze the exact frozen corpus

Run from a dedicated workspace directory. All publishable runner outputs must
remain below the current working directory; the private corpus itself may live
elsewhere.

```sh
python3 tools/apta_1_1_analyze_frozen_corpus.py \
  --corpus-root /private/apta-corpus \
  --staging-labels /private/apta-staging.csv \
  --manifest qualification/manifest.json \
  --analyzer build-qual/tools/apta-analyze \
  --output-dir qualification/run/analyzed \
  --mapping-output qualification/run/mapping.csv \
  --run-metadata-output qualification/run/run.json \
  --source-revision "$REV"
```

The runner always requests `--features all`. Each output is named by opaque
track ID. `run.json` binds the run to:

- exact source revision;
- analyzer binary SHA-256;
- frozen manifest SHA-256;
- feature set;
- per-track `.apta` SHA-256;
- mapping SHA-256.

The state file is written after each completed track. An interrupted run can be
continued with `--resume`. Existing outputs are reused only when the run header
matches and each stored `.apta` hash still matches the file on disk. A changed
source revision, analyzer binary, manifest or output invalidates reuse.

## 4. Export FINAL native DJ results

```sh
python3 tools/apta_1_1_export_acceptance_results.py \
  --inspector build-qual/tools/apta-inspect \
  --mapping qualification/run/mapping.csv \
  --manifest qualification/manifest.json \
  --output qualification/results.csv
```

The exporter requires exact manifest ID coverage, a completed session, FINAL
`MKEY`, FINAL `MTRD`, and a FINAL global/local beatgrid. Missing or provisional
native results fail the export.

## 5. Run final DJ acceptance scoring

```sh
python3 tools/apta_1_1_dj_acceptance_eval.py \
  --manifest qualification/manifest.json \
  --labels qualification/labels.csv \
  --results qualification/results.csv \
  --output qualification/dj-acceptance-report.json
```

The frozen gates are:

- at least 48 fresh manually verified tracks;
- exact key accuracy >=75%;
- exact meter accuracy >=95%;
- downbeat phase accuracy >=90%;
- beatgrid accuracy >=90%;
- <=5% high-confidence errors per family at confidence >=75;
- beat-period tolerance <=1%;
- cyclic downbeat phase tolerance <=0.10 beat.

Do not change these thresholds after examining the fresh corpus results.

## 6. Run the separate tempo/grid fresh-corpus protocol

The previous development corpus and prior holdout are not fresh evidence. Use
the pre-registered APTA 1.1 tempo/grid evaluation protocol and its required
fresh corpus. Only committed evidence produced under that protocol may close the
`tempo-grid-fresh-corpus` readiness blocker.

## 7. Confidence calibration

Confidence fitting is a separate evidence phase. Train on at least 96 rows and
evaluate on an untouched holdout of at least 48 disjoint rows under the frozen
calibration protocol. Do not expose or enable production
`APTA_FEATURE_CALIBRATED_QUALITY` until the accepted model passes the holdout
gates.

## 8. Physical ESP32-P4 evidence

Collect the physical 30-minute ESP32-P4 qualification evidence only on real
hardware using the frozen hardware-evidence contract. Synthetic CI, host tests
and deterministic capacity calculations are not substitutes for physical
memory/timing/USB/audio coexistence evidence.

## 9. Release readiness

Only after the tempo/grid, confidence, final DJ corpus and physical ESP32-P4
evidence blockers are closed may the release-readiness state become
`freeze-eligible`. That state still does not automatically release 1.1.

Perform the final API/ABI/wire/package matrix against the exact release
candidate, then deliberately update version metadata and create the `v1.1.0`
tag/release. Until then, `VERSION` remains `1.0.1` and APTA 1.0 remains the
stable authority.
