# Reference-support metadata amendment

Date: 2026-10-24. **C0 registration only; implementation and execution disabled.**
Parent: [CT03 scientific contract](COUPLED-TIME-DEVELOPMENT-PROTOCOL.md).
Evidence: [R5 diagnosis](COUPLED-TIME-R5-AUDIT-20261024.md) and
[ordered repair plan](COUPLED-TIME-R5-REPAIR-PLAN-20261024.md).
The [machine amendment](COUPLED-TIME-SUPPORT-METADATA-AMENDMENT.json) binds the
exact historical contracts, results, immutable components and
[closed support schema](COUPLED-TIME-REFERENCE-SUPPORT-SCHEMA.json).

## Allowed change and invariant

The old common scorer emits a complete-context bit but no complementary
incomplete-context field. The missing Counter lookup is zero; it is not a measured
zero incomplete count. The real guard expects199. This amendment registers only
the missing accounting interface, not a new detector or a change to scoring.

Planned helper: `tools/apta_reference_support.py`.
Interface: `reference_support(reference_beats, windows) -> list[dict]`.
Validate nonempty strictly ordered nonnegative integer reference beats, rejecting
booleans/floats, duplicates and absent coverage. Validate1..8 ordered nonoverlapping
half-open windows with integer `first_frame < end_frame`; gaps are permitted.
The caller retains the frozen source/frame-domain checks. Do not add an unrelated
frame ceiling or infer musical support from a coverage bit.

For each window use exactly the existing predicate:

```text
context = reference_beats[0] <= first_frame and end_frame - 1 <= reference_beats[-1]
windows = 1
reference_complete_windows = int(context)
reference_incomplete_windows = 1 - int(context)
```

Missing/unknown coverage is an error or explicit unavailable state outside the
successful proof schema; it is never silently converted to zero incomplete.
The helper validates beats once, so expected work is O(reference beats + windows).
This is host metadata work; no P4 memory/latency budget is asserted.

The versioned evaluator adds only `reference_incomplete_windows` to the four common
diagnostic groups (`ols_nominal`, `ols_actual`, `candidate_nominal`,
`candidate_actual`) and their candidate nominal/actual per-window aliases. The
old complete bit must equal the independently classified bit. Every window,
source, diagnostic group and cohort conserves complete+incomplete=windows.
No other legacy score field may change or be discarded during parity comparison.

The complete/incomplete coverage definition remains reference bracketing, not
candidate success, event density, observed gaps or unknown-successor inference.
Unknown-successor/point masks, abstention and musical completeness retain their
old definitions. Source order, window geometry and ownership stay frozen.

## Version and isolation boundary

Planned evaluator: `tools/apta_1_1_coupled_time_evaluation_v2_metadata.py`.
Reuse unchanged canonical `score_source`, event metrics and16 gates. Version the
minimum evaluation orchestration explicitly; no monkeypatch of scorer globals.
Preserve all13 immutable dependencies and all25 existing R4 component files.
Use a separate metadata runner/bootstrap/driver family under the
[metadata operational contract](COUPLED-TIME-RUNNER-V2-METADATA-PROTOCOL.md).

Reference support is computed only in evaluation after full successful construction
and independent verification. No reference input/helper import is permitted in
pure workers. The evaluation context and upstream envelopes must be checked
before opening the reference manifest or creating a support proof.

Use a new `libapta-coupled-time-evaluation-support-1` payload, a separately hashed
reference-support proof and new metadata operation identities. Keep the scientific
registration SHA as the immutable CT03 parent and bind this amendment alongside
the distinct operational identity. No old scientific payload is rewritten.

## Required parity and scope

The exposed generated control remains100 sources/600 windows,400 complete/200
incomplete contexts. The real registration's401/199 is preserved as an expectation,
not treated as newly measured here. No real reference manifest is read by C0.

After removing only the registered new metadata leaves, all600 saved source/window
diagnostic records must match exactly. All100 candidate bytes, old context bits,
point/unknown masks, raw projections, per-family/event means, paired counters and
16 scientific gate verdicts remain identical. The generated verdict stays
REJECTED (14/16), including its meter/downbeat failures. Format, source/artifact
identities and required proof references change only as explicitly registered;
their new hash chain is verified rather than excluded without validation.

The new proof contains indices and counts, not reference coordinates. It binds the
evaluation context, label-free input, candidate freeze, verification and all three
authority identities. Its per-window/source/cohort sums must agree with each
other, the original manifest order/geometry and all four diagnostic totals.

Strict finite/duplicate-free JSON and exact runtime types precede schema and
cross-field validation. The old v2 validator alone is insufficient for this new
contract; explicit conservation and identity validation are mandatory.

C1 implementation and C2 generated checks follow this registration in separately
committed checkpoints. C3 needs a fresh complete generated attempt, a clean pushed
source, the bound immutable runtime, original topology/caps and a new preflight.
This amendment itself authorizes no candidate/scoring run, real attempt, old-subset
replay, projection repair, transfer, training, native activation or release.
The projection decision remains UNDECIDED and historical real-runtime metadata
remains NOT_AUDITED. VERSION and existing production behavior are unchanged.
