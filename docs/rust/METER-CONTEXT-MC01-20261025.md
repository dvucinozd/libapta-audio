# MC-01: bounded meter-context public prototype

Date: 2026-10-25. Branch: `rust-rewrite`. **PUBLIC_GENERATED_IMPLEMENTATION_PASS**.
Implementation follows [MC-00](METER-CONTEXT-PROTOCOL.md), registered and pushed
before code at `7b47885ea9bacb6f65f72ddfb9db902bcae7d29d`. Measurement uses that
baseline plus SHA-bound new files; this is not a clean-source cohort campaign.
[Aggregate evidence and exact component hashes](METER-CONTEXT-MC01-20261025.json).

## Behavior

The new opt-in pure `apta_meter_context_projection.project_window` calls unchanged
P1 once, preserves all grid/fit/point fields and every existing owned meter. Only
accepted grids without meter examine the bounded neighboring-bar fallback. The
caller explicitly supplies `known_gaps=[]`; unknown/nonempty gaps fail validation.

Fallback reads at most two preceding/two following downbeats plus those owned by
the window. Exact source-valid mapping, unchanged1/10-period tolerance, equal3/4
raw and model ordinal intervals and same-grid owned publication are mandatory.
All eligible evidence is considered: conflicting meter or phase hypotheses abstain
with AMBIGUOUS_CONTEXT. A deterministic first eligible triple/owned witness is
used only when the hypothesis is unique. The returned record retains baseline
meter/status, selected context and actual lookbehind/lookahead spans. This is
noncausal source context; event-count bounds imply no fixed millisecond latency.

Public one-frame boundary shifts in both directions now retain meter in both
windows for3/4 and4/4, where P1 loses its third local downbeat. Raw events are
unchanged by projection. This is a public mechanism check, not a rerun of P6's25
saved cases, a new generated cohort result or musical acceptance.

## Verification

All38 tests pass, zero failures/errors/skips:15 new MC-01 tests,14 unchanged P1
controls and9 unchanged P0 registration controls. New full-record comparisons
against the separately written oracle total826, including501 context meters and
280 exactly preserved P1-owned meters. Two conflicting meter/phase cases abstain;
four no-evidence cases abstain. Thirty-nine cases retain P1's unsupported grid.
The unchanged P1 controls also reproduce446 oracle comparisons and their original
legacy-intersection counts. The new oracle imports the frozen independent P1
oracle and stdlib only; production imports P1 and Fraction only.

Checks include arithmetic matrices beyond the minimal crossing, both meters and
directions, source/window/exclusive endpoints, Q32 fractions and half ties,
out-of-tolerance context, positive context, ambiguity, exact invalid types/order/
geometry/gaps, explicit source-window point accounting, input invariance, ignored
label environment and a fresh process that denies I/O during construction. The
3072-event high-density input correctly retains UNSUPPORTED_TEMPO; no fabricated
fit is used to force an unreachable accepted grid at that density.

New public suite wall time is1.889s; maximum paired production/oracle public call
is0.027422s. Worker self RSS/HWM is26884KiB. Parent supervisor's observed157296KiB
is reported separately and includes its own boundary-audit storage. P1/P0 host
controls take3.513/1.527s with36328/32040KiB observed RSS. External evidence at
publication is72741bytes. All are within unchanged host caps; these are test costs,
not isolated production allocations or device/inference measurements.

The public MC-01 suite runs in the existing read-only parent-runtime/source
namespace; actual new loaded-source/runtime hashes are retained. Parent inventory
verifies before/after. The first old-control launch in that parent's venv stops
before tests because jsonschema is absent. Its failed logs remain. The23 old
controls then pass in the existing host Python with actual loaded bindings saved.
No package, frozen parent image or old test/source is changed. This evidence does
not qualify a new complete operational runtime closure.

All73 P5 components,14 P0 documents,17 reviewed sources,37 frozen C3 components,
four P2 probe components and12 MC-00 bindings still verify. The original projector,
solver, scoring paths, old campaigns and VERSION1.0.1 remain unchanged. New files
are exactly the three paths registered by MC-00. No full C/Rust/device matrix ran.

Reproduce public unit checks from this repository using:

```powershell
wsl -- python3 -B -X pycache_prefix=/libapta-no-cache /mnt/d/AI/LIBAPTA/libapta-audio/tests/unit/apta_1_1_meter_context.py
```

Measured namespace commands, full runtime bindings and retained failed-control
logs are external under `D:\AI\LIBAPTA\outputs\meter-context-mc01-20261025`.

## Next boundary

MC-02 should minimally adapt the existing generated PROJECT/VERIFY/EVALUATE
machinery to this separate module/protocol, bind its new loaded source closure
and qualify operational fixtures. Reuse the existing canonical evaluation,
independent oracle, ordered phase chain and resource enforcement. Register one
fresh generated matched comparison only after that operational evidence passes.
Neither old P5/P6 authorization nor this unit receipt dispatches new cohort work.

P6 remains rejected. Historical real-runtime H0 is NOT_AUDITED; independence and
confidence remain unqualified. The [fresh product budget audit](METER-CONTEXT-P4-BUDGET-20261025.md)
still has UNKNOWN whole-pipeline allocations and deadlines. There is no saved-
cohort scoring, real/GTZAN run, model training/inference, consumer activation,
physical P4, fresh acceptance or release at MC-01.
