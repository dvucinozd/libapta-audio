# APTA 1.1 acceptance workflow

The canonical detailed procedure is `docs/status/APTA-1.1-QUALIFICATION-RUNBOOK.md`; use the repository scripts rather than reimplementing them.

## Corpus boundary

Use at least 48 tracks that were independently verified and were not used to tune the candidate. Canonicalize to 48 kHz stereo PCM16 WAV, label those exact WAV frames, and keep all private source material and mappings local. Freeze labels with `tools/apta_1_1_freeze_corpus.py`; do not use `--allow-diagnostic` for final acceptance.

## Exact run order

1. Build `apta-analyze` and `apta-inspect` from a clean exact revision with tests and warnings-as-errors enabled.
2. Freeze the canonical manifest and opaque labels.
3. Run `tools/apta_1_1_analyze_frozen_corpus.py` with `--features all`; bind the run to the full source SHA and hashes.
4. Export only completed FINAL native results with `tools/apta_1_1_export_acceptance_results.py`.
5. Score with `tools/apta_1_1_dj_acceptance_eval.py`.
6. Preserve the JSON report and gate-by-gate verdict. A passing evaluator self-test is not corpus evidence.

Do not tune from a fresh acceptance result. If a candidate changes, the corpus is spent and a new independently verified corpus is required.

## Frozen gates

- minimum 48 fresh manually verified tracks;
- key exact >=75%;
- meter exact >=95%;
- downbeat phase >=90%;
- beatgrid >=90%;
- beat-period tolerance <=1%;
- cyclic downbeat phase tolerance <=0.10 beat;
- high-confidence errors <=5% for every output family at confidence >=75.

All gates are conjunctive. No weighted average can compensate for a failed family. Thresholds and correctness definitions must not be changed after inspecting fresh results.

## Release boundary

After the DSP/corpus gates pass, collect physical ESP32-P4 memory/timing/USB/audio-coexistence evidence. Then run `release/check_1_1_readiness.py`, generate the pre-freeze snapshot from a clean exact checkout, perform the deliberate API/ABI/wire/version finalization, rerun the complete release matrix, and only then tag/publish. Until that point `VERSION` remains `1.0.1` and the branch is development-only.
