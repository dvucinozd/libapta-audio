# Ballroom native development baseline

Registered 2026-10-22 before acquisition, preparation or native analysis.
Baseline: `1876eb34a9169299ece0d908927850e6f6e79670` plus the retained dirty
ASAP tooling inventory
`370de33dfbe688b31692f3706806885c9460fbbc0012cb10e7eab2fc571570fc`.
Hypothesis: the previously frozen Ballroom development partition supplies a
recorded-audio baseline for the unchanged native engine against dense source
beat/downbeat annotations. This step changes data plumbing only, not DSP.

## Frozen boundaries

Reuse the canonical Ballroom seed and selection: twenty 3/4 and twenty 4/4
development recordings. Preserve the old combined-split preparation behavior;
add an explicit development-only mode that refuses nonempty output and source
audio directories. Use archive headers and annotation metadata for the existing
698-track inventory; materialize/decode only the forty selected development
audio members. The complete compressed distribution contains holdout bytes;
transport download/decompression for tar traversal is not a claim of zero
holdout bytes fetched. No holdout WAV is extracted, decoded or analyzed. Keep
formal holdout evaluation closed, including ASAP and the fresh DJ pool.

Require published transport checksums (audio MD5
`2872a3e52070bc342a4510a95e2fa0b8`, annotations MD5
`d0c31e1a30c0caf8fd22dec25f2174cf`) and record SHA-256 identities privately.
Preserve original durations through canonical 48 kHz stereo PCM16 conversion;
do not trim or pad to manufacture exact thirty-second sources. Analyze at most
the first thirty seconds with the existing bounded history runner. Preserve
dense annotation coordinates in source frames. Retain private license/source
provenance and opaque content IDs; commit no audio, labels or source mappings.
The published [mirdata loader](https://mirdata.readthedocs.io/en/stable/_modules/mirdata/datasets/ballroom.html)
records the distribution URLs, transport checksums and CC BY-NC-SA 4.0 terms.

Use the identical existing coarse, sparse and energy-novelty native binary.
Extend the annotated-development loader with the explicit Ballroom format,
checksum and development-only declaration; retain ASAP revision/geometry checks.
Do not change `score_window`, its nearest-five local reference period, 1%
period tolerance, 0.10-beat phase tolerance or reference-completeness rule.
Report every source window, conditional complete-reference counts including
abstentions, per-beat period/phase, meter/downbeat availability and correctness,
and high-confidence errors/unknown context, separately for 3/4 and 4/4.
No rejected event oracle or new selector is run. No tuning after results.

Freeze the dirty tooling snapshot before the native run. Any selection,
identity, geometry, metadata, binary drift, holdout extraction, incomplete
drain, unavailable evidence or capacity loss invalidates the run. Mock tests
must demonstrate development-only extraction/preparation and legacy default
behavior without real holdout access. Cost is forty local canonical files and
120 serial native processes; record desktop wall/RSS. No native state or
resource budget changes. Fixes/breaks are inapplicable without a new candidate;
this is baseline evidence only, never FINAL, release or physical P4 acceptance.

After both external baselines, separately pre-register a new bounded temporal
and metrical model. Do not revive the rejected event-support formula by tuning
these development results.

## Completed preparation and results

Both archives match the published MD5 checksums. Their SHA-256 values are
`e2f85f5ac230523498ca3b9addf225de19f1d4d1cb7d8724a40bc0044b7c5947`
(audio) and
`c960ae5c69a30c4fddf1331577e57b11a7fc68d84f3d65dbd79a962041a1c81e`
(annotations). Annotation metadata supplies the unchanged deterministic split.
Exactly forty selected development WAVs are materialized and canonicalized;
the source-file set matches the registered development cohort and is disjoint
from the formal holdout cohort. Zero holdout WAVs are extracted, decoded or
analyzed. The compressed transport archive still contains all source payloads.
Source/license metadata and per-source hashes remain in private external maps.

Conversion uses ffmpeg 6.1.1-3ubuntu5. Original durations produce 1437673 to
1525821 canonical frames: one recording is shorter than thirty seconds, the
other thirty-nine longer. The native prefix run consumes 57597673 frames per
mode, preserving the short source without padding and ignoring post-thirty-
second audio only for analysis. All three modes finish all 240 source windows
without capacity loss, lost evidence or incomplete drain. Annotations fully
cover 165 windows; 75 boundary windows remain explicitly incomplete.

| Measure | Coarse S6 | Sparse | Energy novelty |
|---|---:|---:|---:|
| Emitted timing / all windows | 239/240 | 0/240 | 0/240 |
| Confirmed period / reference-complete windows | 27/165 | 0/165 | 0/165 |
| Confirmed all-beat phase / reference-complete windows | 31/165 | 0/165 | 0/165 |
| Confirmed joint period and phase / reference-complete windows | 9/165 | 0/165 | 0/165 |
| Predicted beats inside annotations | 2398/2479 | 0/0 | 0/0 |
| Correct phase among scored beats | 836/2398 | 0/0 | 0/0 |
| Correct local period among scored beats | 708/2398 | 0/0 | 0/0 |

Zero-denominator beat ratios are unavailable. Joint confirmed correctness is
also 9/240 unconditionally; incomplete annotation context is never inferred
correct. Coarse meter is correct in 107/196 emissions, downbeat in 21/188
scorable meter records. Thirteen coarse grids have confidence >=75: three are
joint-correct with complete reference context, four are wrong, six have unknown
boundary context. These per-window observations establish no confidence-safety
qualification. Sparse/novelty non-emissions do not establish accuracy or safety.

| Coarse S6 by reference meter | 3/4 | 4/4 |
|---|---:|---:|
| Emitted grid | 119/120 | 120/120 |
| Joint confirmed / reference-complete windows | 0/84 | 9/81 |
| Correct meter / meter emissions | 35/99 | 72/97 |
| Correct downbeat / scorable meter records | 14/97 | 7/91 |

This is the unchanged engine's recorded-music development baseline, not a new
candidate comparison. No fixes/breaks or new verdicts are attributed to the
tooling. Neither formal FINAL acceptance, historical C track-level metrics,
the rejected DJ event oracle nor physical P4 evidence is replaced by this run.
The poor phase, triple-meter and downbeat results leave the native end-to-end
completion criterion open. No DSP threshold or selection rule was changed.

## Reproduction and verification

```sh
python3 tools/apta_1_1_ballroom_meter_validation.py prepare \
  --source /data/annotation-metadata-with-empty-audio-directory \
  --audio-archive /data/data1.tar.gz --annotation-archive /data/master.zip \
  --ffmpeg /usr/bin/ffmpeg --output /new/ballroom-development --development-only
python3 tools/apta_1_1_native_annotated_development.py \
  --prepared /new/ballroom-development --analyzer /build/apta-history-diagnostic \
  --output /new/native-baseline \
  --source-revision 1876eb34a9169299ece0d908927850e6f6e79670 \
  --candidate-inventory-sha256 <exact-dirty-tooling-inventory-sha256>
```

Eight Ballroom preparation tests, six dense-reference tests and nine ASAP
preparation tests pass. Fixtures exercise actual selective tar extraction,
duplicate/link/unsafe member rejection, output/source reuse refusal, balanced
development metadata, legacy both-split behavior, source-coordinate scoring,
unchanged strict ASAP duration checks and variable Ballroom durations. The
actual existing ASAP development-only preparation remains accepted. The
scoring function's AST and every native processing/runtime/test-driver source
hash match the preceding ASAP checkpoint; the analyzer binary is identical.
The earlier 289 default / 323 all-feature debug/release and 61 focused sanitizer
tests remain historical unchanged-code evidence; no Rust/C/product/P4 matrix
was rerun for this Python-only step.

Archive/cohort verification, 120 serial native processes, hashing and dense
scoring take 18.66 seconds wall (17.66 user / 1.29 system), maximum RSS 30324 KiB
in WSL. Acquisition, canonicalization and test time are separate. This is host
cost, not P4 resource or realtime qualification.

Private data, source snapshot and provenance are external under
`/home/daniel/.local/share/libapta-audio/rust-rewrite/ballroom-development-20261022/`.
Aggregate reports and scripts are under
`D:/AI/LIBAPTA/outputs/rust-ballroom-development-20261022/`.

- Frozen tooling inventory SHA-256:
  `b4ead30f1e3662968af718389b1b785eef9eca288634453566e30e3e5be1d894`.
- Development-only prepared manifest SHA-256:
  `488d2a66cc8858220054c779128e097a28b367a47ab793b8ec3f4a39112a4a46`.
- Dense labels SHA-256:
  `50e0713696685ec27e29bc149c061741c6d97901e67651c12778506c84d9b00d`.
- Dense native baseline report SHA-256:
  `6449c890580f9702e5da3efebd268b98b668571cee28eb27b8fd446872c60e17`.

The successor [dense temporal model](S6-TEMPORAL-MODEL-EXPERIMENT.md) is now
registered with exact hypotheses, resource limits and stop rules. Its next
stage is native dense-array capture and unchanged-output verification, before
any selector scoring. Keep the rejected event selector frozen and use these
two open external baselines as development regressions; all formal holdouts and
fresh acceptance inputs stay outside that probe. Native consumer adoption and
P4 acceptance remain later gates. Production defaults, stable version and
consumer pin stay unchanged.
