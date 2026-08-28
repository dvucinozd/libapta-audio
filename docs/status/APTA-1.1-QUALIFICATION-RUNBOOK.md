# APTA 1.1 qualification runbook

This runbook defines the reproducible path from a private manually labelled DJ
audio corpus to the frozen APTA 1.1 qualification reports. It does not provide
acceptance evidence by itself.

## Evidence boundary

- Real audio remains local and is never committed to the repository.
- Original MP3/FLAC/WAV source material may remain private in any convenient
  directory.
- Qualification frame labels are bound to a canonical PCM WAV, not to compressed
  MP3/FLAC timing.
- The local preparation manifest and staging CSV may contain private filenames
  or relative paths and must not be committed.
- Frozen labels use only opaque IDs derived from full canonical-WAV SHA-256
  digests.
- The corpus runner re-hashes every canonical WAV and requires an exact match
  with the frozen manifest before analysis starts.
- `apta-analyze` is invoked with an anonymous input name of the form
  `track-<hash>.wav`, so the original filename/path cannot enter `.apta`
  application-source metadata.
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

## 1. Prepare canonical WAV files

For final DJ acceptance, use at least 48 genuinely fresh, manually verified
tracks that were not used to tune the candidate. A larger private pool is
recommended so ambiguous tracks can be excluded before freezing.

The current reference desktop analyzer opens WAV directly. MP3 and FLAC are
valid source material, but must first be converted to the exact PCM stream that
will be labelled and analyzed.

Use the corpus preparation tool:

```sh
python3 tools/apta_1_1_prepare_corpus.py \
  --source-root /private/apta-raw \
  --output-root /private/apta-canonical \
  --manifest-output /private/apta-preparation.json
```

It recursively accepts `.mp3`, `.flac` and `.wav` and produces 48 kHz stereo
PCM16 WAV files with source metadata removed. It records source/canonical
SHA-256 values and the FFmpeg version in a **local-only** manifest. Do not commit
that manifest.

The canonical WAV is the authority for frame coordinates and frozen identity.
Do not label an MP3/FLAC timestamp and then analyze a separately converted WAV.

Detailed instructions are in
[`APTA-1.1-CORPUS-LABELING.md`](APTA-1.1-CORPUS-LABELING.md).

## 2. Label the private corpus

Open `tools/apta_1_1_label_corpus.html` locally in a modern browser. The helper
contains no remote scripts and does not upload audio.

For each canonical WAV record:

- `key_tonic` (`0..11`);
- `key_mode` (`major` or `minor`);
- meter (`3/4` or `4/4`);
- a manually verified downbeat frame;
- beat period in source frames.

The helper parses the WAV header itself for exact sample rate/frame geometry.
For beat period, prefer a reference beat 16–64 beat intervals after the marked
downbeat; this averages manual cursor error better than tap tempo.

Export `staging_labels.csv` with schema:

```text
source,key_tonic,key_mode,meter_numerator,meter_denominator,downbeat_frame,beat_period_frames
```

`source` is relative to `/private/apta-canonical`. Keep the staging CSV private.

## 3. Freeze the corpus

After an independent label review:

```sh
python3 tools/apta_1_1_freeze_corpus.py \
  --corpus-root /private/apta-canonical \
  --staging-labels /private/staging_labels.csv \
  --labels-output qualification/labels.csv \
  --manifest-output qualification/manifest.json \
  --frozen-utc 2026-01-01T00:00:00Z \
  --reference-source manually-verified-fresh-corpus \
  --verification-procedure independent-dj-label-review
```

Do not use `--allow-diagnostic` for final acceptance. The freezer rejects fewer
than 48 tracks, duplicate audio content, invalid labels, missing files and path
escape from the corpus root.

## 4. Analyze the exact frozen corpus

Run from a dedicated workspace directory. All publishable runner outputs must
remain below the current working directory; the private corpus itself may live
elsewhere.

```sh
SOURCE_DATE_EPOCH="$FROZEN_EPOCH" \
python3 tools/apta_1_1_analyze_frozen_corpus.py \
  --corpus-root /private/apta-canonical \
  --staging-labels /private/staging_labels.csv \
  --manifest qualification/manifest.json \
  --analyzer build-qual/tools/apta-analyze \
  --output-dir qualification/run/analyzed \
  --mapping-output qualification/run/mapping.csv \
  --run-metadata-output qualification/run/run.json \
  --source-revision "$REV"
```

The runner always requests `--features all`. Each output is named by opaque
track ID. `run.json` binds the run to exact source revision, analyzer SHA-256,
frozen manifest SHA-256, feature set, per-track `.apta` SHA-256 and mapping
SHA-256.

Set `SOURCE_DATE_EPOCH` to one frozen non-negative Unix timestamp for every
baseline and candidate run. `apta-analyze` records that value in META instead
of wall-clock time, making complete container hashes reproducible. An invalid
or empty value is rejected rather than silently falling back to the clock.

An interrupted run can continue with `--resume`. Existing outputs are reused
only when the run header matches and each stored `.apta` hash still matches the
file on disk.

## 5. Export FINAL native DJ results

```sh
python3 tools/apta_1_1_export_acceptance_results.py \
  --inspector build-qual/tools/apta-inspect \
  --mapping qualification/run/mapping.csv \
  --manifest qualification/manifest.json \
  --output qualification/results.csv
```

The exporter requires exact manifest ID coverage, a completed session, FINAL
`MKEY`, FINAL `MTRD`, and a FINAL global/local beatgrid. Because the native
`MTRD` result is resolved against the refined S4 grid, the exporter uses FINAL
`LGRD` for the scored beat period whenever it is present and falls back to
FINAL `GGRD` only when no local grid is available. This keeps the exported
period and downbeat on one grid. Missing or provisional native results fail the
export.

### Development candidate comparison (not acceptance)

Before opening a holdout, compare two exported result sets against the same
frozen labels. The comparison tool reports fixes, breaks, changed verdicts,
exact accuracy, high-confidence errors, execution failures and an optional
exact resource delta for key, meter, downbeat and beatgrid. Missing result rows
remain in the denominator and count as execution failures; malformed, duplicate
or out-of-manifest rows are rejected.

```sh
python3 tools/apta_1_1_compare_dj_candidates.py \
  --manifest qualification/manifest.json \
  --labels qualification/labels.csv \
  --baseline-results qualification/baseline-results.csv \
  --candidate-results qualification/candidate-results.csv \
  --baseline-name default \
  --candidate-name all-current-experiments \
  --baseline-revision "$BASELINE_REV" \
  --candidate-revision "$CANDIDATE_REV" \
  --candidate-flag APTA_ENABLE_EXPERIMENTAL_MULTIBAND_ONSET=ON \
  --candidate-flag APTA_ENABLE_EXPERIMENTAL_3BAND_DOWNBEAT=ON \
  --candidate-flag APTA_ENABLE_EXPERIMENTAL_KEY_TUNING=ON \
  --candidate-flag APTA_ENABLE_EXPERIMENTAL_HARMONIC_HPCP=ON \
  --evidence-level development \
  --corpus-status spent \
  --output qualification/candidate-comparison.json
```

Both revisions must be full 40-character Git SHAs. Flags are sorted and
deduplicated in the deterministic JSON report. `--baseline-resources` and
`--candidate-resources` may name JSON objects with identical numeric keys; when
present, the report includes the exact candidate-minus-baseline delta. The
report always carries `acceptance_claim: false`, including when
`--evidence-level holdout` is used.

## 6. Run final DJ acceptance scoring

```sh
python3 tools/apta_1_1_dj_acceptance_eval.py \
  --manifest qualification/manifest.json \
  --labels qualification/labels.csv \
  --results qualification/results.csv \
  --output qualification/dj-acceptance-report.json
```

Frozen gates:

- at least 48 fresh manually verified tracks;
- exact key accuracy >=75%;
- exact meter accuracy >=95%;
- downbeat phase accuracy >=90%;
- beatgrid accuracy >=90%;
- <=5% high-confidence errors per family at confidence >=75;
- beat-period tolerance <=1%;
- cyclic downbeat phase tolerance <=0.10 beat.

Do not change thresholds after examining the fresh corpus results.

## 7. Separate tempo/grid fresh-corpus protocol

The previous development corpus and prior holdout are not fresh evidence. Use
the pre-registered APTA 1.1 tempo/grid evaluation protocol and its required
fresh corpus. Only committed evidence produced under that protocol may close the
`tempo-grid-fresh-corpus` readiness blocker.

### Targeted 3/4 meter/downbeat development protocol

This protocol is development evidence only and cannot close the final DJ
acceptance blocker. It creates deterministic, balanced development and holdout
splits, then allows only the development split to be analyzed while a candidate
is being designed.

For ASAP v1.2, install `mido==1.3.3` and `numpy==2.3.5` in a local environment
and run:

```sh
python3 tools/apta_1_1_asap_meter_validation.py prepare \
  --asap-root /private/asap-dataset-v1.2 \
  --output /private/apta-asap-meter
```

For Ballroom, download the audio and annotation archives from the URLs recorded
by the official `mirdata` loader, extract them below one source directory, and
retain the original archives so the preparation step can enforce their MD5s:

```sh
python3 tools/apta_1_1_ballroom_meter_validation.py prepare \
  --source /private/ballroom-source \
  --audio-archive /private/downloads/data1.tar.gz \
  --annotation-archive /private/downloads/master.zip \
  --ffmpeg /usr/bin/ffmpeg \
  --output /private/apta-ballroom-meter
```

Analyze and score only `development` during candidate work:

```sh
REV=$(git rev-parse HEAD)
python3 tools/apta_1_1_ballroom_meter_validation.py run \
  --prepared /private/apta-ballroom-meter \
  --analyzer build-qual/tools/apta-analyze \
  --output /private/apta-ballroom-run \
  --split development \
  --source-revision "$REV"
python3 tools/apta_1_1_ballroom_meter_validation.py evaluate \
  --prepared /private/apta-ballroom-meter \
  --split development \
  --inspector build-qual/tools/apta-inspect \
  --mapping /private/apta-ballroom-run/mapping.csv \
  --report /private/apta-ballroom-run/report.json
```

Do not substitute `holdout` in either command until the candidate and all
ordinary regressions are frozen. A holdout may be run once for a retain/reject
decision and must not be used for repeated parameter tuning.

## 8. Confidence calibration

Confidence fitting is a separate evidence phase. Train on at least 96 rows and
evaluate on an untouched holdout of at least 48 disjoint rows under the frozen
calibration protocol. Do not expose or enable production
`APTA_FEATURE_CALIBRATED_QUALITY` until the accepted model passes the holdout
gates.

## 9. Physical ESP32-P4 evidence

Collect the physical 30-minute ESP32-P4 qualification evidence only on real
hardware using the frozen hardware-evidence contract. Synthetic CI, host tests
and deterministic capacity calculations are not substitutes for physical
memory/timing/USB/audio coexistence evidence.

## 10. Release readiness and pre-freeze snapshot

Only after the tempo/grid, confidence, final DJ corpus and physical ESP32-P4
evidence blockers are closed may release readiness become `freeze-eligible`.
That state still does not automatically release 1.1 and must retain the
`1.0.1` development package identity with no `v1.1.0` tag.

Verify the exact clean checkout first:

```sh
REV=$(git rev-parse HEAD)
python3 release/check_1_1_readiness.py \
  --root . \
  --manifest release/1.1-readiness.json \
  --expect freeze-eligible \
  --output build/release/apta-1.1-readiness.json
```

Then generate the pre-freeze snapshot **before any version bump or release
finalization changes**:

```sh
python3 release/generate_1_1_freeze_snapshot.py \
  --root . \
  --source-revision "$REV" \
  --output build/release/apta-1.1-freeze-snapshot.json
```

The generator fails unless readiness passes as `freeze-eligible`, the tracked
worktree is clean, the checkout exactly matches `REV`, and every required
evidence/API/ABI/wire input is committed and byte-identical to HEAD. The
snapshot binds the readiness manifest, all four external evidence files and all
policy-required freeze inputs with SHA-256 and Git blob SHA-1.

Preserve that snapshot as release-freeze evidence. It is **not** a release
manifest, does not bump any version, and does not create a tag. Version/API/spec
finalization remains a deliberate subsequent commit whose exact policy must be
reviewed before it is performed.

After that deliberate finalization, rerun the full native/shared/ILP32/
ESP-IDF/P4/fuzz/container/package matrix against the exact release candidate.
Only a candidate that passes that complete matrix may proceed to the final
`v1.1.0` tag/release. Until then, `VERSION` remains `1.0.1` and APTA 1.0 remains
the stable authority.
