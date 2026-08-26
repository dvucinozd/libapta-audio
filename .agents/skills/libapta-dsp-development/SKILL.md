---
name: libapta-dsp-development
description: Run reproducible LIBAPTA DSP experiments, corpus evaluations, and APTA 1.1 release-gate checks. Use for musical-key, meter, downbeat, beatgrid, tempo, confidence, corpus, or acceptance work in the libapta-audio repository; do not trigger for unrelated API or documentation edits.
---

# LIBAPTA DSP development

Use this skill to keep DSP research and release evidence separate, repeatable, and fail-closed.

## Start with the repository boundary

1. Work from the `libapta-audio` repository root. Check `git status --short --branch`, the full `HEAD` SHA, and whether the checkout is clean before producing evidence.
2. Read `docs/status/APTA-1.1-DEVELOPMENT-STATUS.md` for the current blockers. For corpus work, also read `docs/status/APTA-1.1-QUALIFICATION-RUNBOOK.md` and `docs/status/APTA-1.1-DJ-ACCEPTANCE-PROTOCOL.md`.
3. Treat the committed status documents and frozen evaluator as the authority. Do not infer release readiness from a passing unit-test run.
4. Keep private audio, Rekordbox databases, source-to-canonical mappings, staging labels, and private manifests outside the repository. Never commit them.

## Choose the operating mode

- **Audit:** report the current branch, blockers, available evidence, and reproducible commands without changing DSP behavior.
- **Candidate experiment:** make the smallest bounded change, keep it opt-in until transfer evidence exists, and compare it with a recorded baseline.
- **Corpus qualification:** use only the frozen workflow in [references/acceptance-workflow.md](references/acceptance-workflow.md). A corpus with fewer than 48 independently verified tracks is diagnostic-only.
- **Release closure:** require every frozen gate, physical ESP32-P4 evidence, a clean exact checkout, and the release freeze procedure. Do not bump `VERSION` or create `v1.1.0` early.

## DSP experiment rules

Before editing code, state the hypothesis, evidence source, expected resource cost, and no-regression veto. Preserve a baseline artifact and change one evidence axis at a time. Prefer genuinely new evidence (for example harmonic/tuning evidence for key or onset/temporal evidence for beatgrid) over more scoring rules over the same exhausted signal.

For every candidate:

- keep new behavior behind an explicit experimental option until independent transfer evidence supports promotion;
- run the relevant native tests with warnings-as-errors, then sanitizer coverage when practical;
- record fixes, breaks, changed verdicts, confidence-safety errors, memory/state cost, and whether the corpus is spent or untouched;
- stop a probe when its predeclared no-regression veto fails; do not rescue it by changing thresholds after seeing results;
- never open a formal holdout repeatedly for tuning.

The current final gates are conjunctive: at least 48 fresh manually verified tracks, key >=75%, meter >=95%, downbeat phase >=90%, beatgrid >=90%, beat-period error <=1%, cyclic downbeat phase error <=0.10 beat, and no more than 5% high-confidence errors per family. The previously used 60-track corpus is spent and its official result was rejected; it cannot become fresh evidence for a tuned candidate.

## Report completion clearly

End each run with: exact source revision, candidate flags, commands/tests run, machine-readable report paths, gate-by-gate metrics, evidence level, remaining blockers, and `git status`. Distinguish implementation correctness from algorithmic acceptance and from physical-device evidence.

Read [references/dsp-experiment.md](references/dsp-experiment.md) for candidate design and evaluation discipline. Read [references/acceptance-workflow.md](references/acceptance-workflow.md) before any corpus freeze, analysis, export, evaluator run, or release-readiness check.
