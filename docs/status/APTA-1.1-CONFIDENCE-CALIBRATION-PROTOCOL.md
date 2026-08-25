# APTA 1.1 confidence calibration protocol

**Status:** pre-registered before fitting any APTA 1.1 calibrated-confidence model

## Scope

This protocol defines the first Task-6 checkpoint for tempo confidence. It does
not itself claim a calibrated production model and it does not modify the
stable APTA 1.0 confidence semantics.

The 1.1 result model already provides `apta_quality_view_t`, including a
`calibration_model_id`, evidence coverage, calibrated confidence, feature state
and quality flags. A model may populate that optional quality record only after
passing this protocol.

## Data separation

Training and holdout sets must contain opaque IDs only in tracked evidence. They
must be disjoint before fitting begins.

Each CSV row contains:

- `id`: opaque sample ID;
- `raw_confidence`: integer 0..100 emitted by the unchanged detector;
- `correct`: 0 or 1 against the frozen reference label.

Minimum sizes for acceptance evidence:

- training: 96 rows;
- holdout: 48 rows.

Smaller sets are diagnostic-only. Reusing the Task-5 fresh acceptance set as the
Task-6 holdout is forbidden if its labels or candidate outcomes were inspected
during model design.

## Frozen fitting method

The first candidate uses deterministic isotonic regression (pair-adjacent
violators) over raw confidence 0..100:

1. aggregate identical raw-confidence values;
2. fit a non-decreasing empirical correctness probability;
3. expand the fitted mapping to a 101-entry lookup table;
4. convert probabilities to integer confidence values 0..100 by nearest-integer
   rounding;
5. derive `calibration_model_id` from the canonical model JSON content.

No hidden threshold, genre feature, tempo-range feature, artist/title metadata
or corpus-specific identifier enters the model.

## Holdout metrics

Report both raw and calibrated metrics over the exact same frozen holdout:

- Brier score;
- 10-bin expected calibration error (ECE);
- errors among rows whose reported confidence is >=75;
- mean reported confidence;
- empirical accuracy.

## Acceptance

A tempo calibration candidate is accepted only when all conditions hold on a
holdout of at least 48 rows:

1. calibrated Brier score is not worse than raw Brier score;
2. calibrated ECE is not worse than raw ECE;
3. high-confidence error count does not increase;
4. at least one of Brier score, ECE, or high-confidence error count improves
   strictly;
5. the model is deterministic for identical training bytes;
6. training and holdout opaque IDs are disjoint;
7. no native, ABI, ESP-IDF, sanitizer, fuzz or container regression is
   introduced when the model is integrated.

Passing this protocol qualifies only the specific frozen model identified by
its model ID and training hash. Any change to fitting policy or training data
creates a different model ID and requires a new holdout evaluation.

## Integration boundary

Until a model passes this protocol, production code must not set
`APTA_FEATURE_CALIBRATED_QUALITY` merely by re-labeling an existing raw
confidence value. A future integration commit may add the accepted tempo model
as a bounded static lookup table and publish an optional quality record for
`APTA_FEATURE_BPM`; that commit requires the full repository CI matrix.

## Task-6 execution record — 2026-08-25

Recorded before any candidate run.

Training rows (all harvested with one HEAD binary in production
`--request-global` mode, opaque IDs only):

| Source | Rows | Reference |
|---|---:|---|
| Historical 188-track Rekordbox corpus (endorsed run of 2026-08-25) | 188 | Rekordbox PQTZ modal tempo |
| Ballroom development partition | 40 | hand-corrected annotation median interval |
| ASAP development partition (synthesized performances) | 40 | score-derived annotations |
| Automated DJ diagnostic corpus (contaminated development evidence) | 60 | Rekordbox provisional BPM |

Total training pool: **328 rows** (>= 96 required).

Holdout set — frozen before the first APTA touch:

- rule: first 48 stems by `sha256("apta-task6-holdout-v1:" + stem)` over
  Ballroom annotation stems never selected by any prior prepared split
  (618 eligible, 0 APTA runs ever);
- canonicalized to 48 kHz stereo s16 WAV from source audio;
- reference tempo: median annotated inter-beat interval per track;
- selection file retained locally with the exact stem list; no outcome row
  has been read or computed at freeze time.
