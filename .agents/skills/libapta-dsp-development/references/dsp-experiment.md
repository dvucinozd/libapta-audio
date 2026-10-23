# DSP experiment protocol

Use for a candidate affecting key, meter, downbeat, beatgrid, tempo or confidence.
Follow the active branch's committed handoff and registered protocol for the next
algorithm; this reference does not prescribe a permanent research direction.

## Register the candidate

Before coding, record the hypothesis, concrete failure mode, baseline source/output,
metric definitions, input identities, prior exposure and known training overlap.
Define allowed constructor inputs, output representation, unknown-support policy,
fixed parameters, resource caps, paired no-regression veto and stop/retry policy.
If the active plan already fixes these, implement it rather than designing another
candidate. Synthetic preflight may reveal a bug; document repairs before real
evaluation. After real results, do not tune the failed candidate or quietly amend
its protocol. Preserve rejected outputs and their hashes.

## Choose the implementation boundary

| Changed layer | Isolation and relevant verification |
|---|---|
| C/native detector | Opt-in CMake/build option; relevant Werror, conformance and sanitizer checks |
| Rust core/runtime | Cargo feature or experimental executable; fmt, focused Clippy/tests, applicable no_std/no-allocation and interoperability contracts |
| Python host experiment | Separate opt-in command/pure constructor; unit/oracle tests, deterministic output, input isolation and resource enforcement |
| Documentation/registration | Schema consistency, links, identities and diff checks; no algorithm or corpus PASS claim |

Preserve default state layout, output semantics and serialization unless the
registered experiment covers a change. Keep baseline/native output identity when
it is a control. Test changed decisions and failure modes; avoid tests that merely
repeat implementation details. Do not rerun unrelated full matrices for every
host-only or documentation checkpoint.

Use canonical runners/scorers. Read the exact function and coordinate contract
before adapting inputs; do not infer units or fields from filenames or silently
change scoring to accommodate a new representation.

## Evaluate in bounded stages

1. Pass focused synthetic/operational preflight.
2. Freeze exact source, protocol, inputs and outputs at the prescribed boundaries.
   Construct all label-blind candidates before loading reference labels for scoring.
3. Run the registered cohort/order and retain every required source and window.
   Compare paired fixes/breaks, unsupported output and safety, not just averages.
4. Stop on the registered veto. Integrity/resource failures yield INVALID or
   INCOMPLETE; a complete valid musical gate failure yields REJECTED.
5. Only a passing candidate proceeds to separately registered transfer. Verify
   recording/performance and model-training independence first. Ballroom/ASAP or
   another named corpus is not automatically independent.
6. Only after required development and transfer gates, enter final acceptance
   on a fresh independently verified corpus under its separate frozen workflow.

An implementation repair is not automatically an authorized real-data retry.
Follow the registered retry policy, preserve failed attempts and disclose whether
labels/results were exposed. Do not invent retries, sweep parameters or overwrite
outputs. A new hypothesis needs explicit registration and appropriate task scope.

## Compare like with like

Keep event F1/CMLt, nominal grid period/phase, actual outgoing intervals,
meter/downbeat and confidence separate. Preserve reference context, tolerances,
trim, projection and missing-successor rules. Compare matched definitions and
denominators. A reference-label construction proves representability, not detector
quality. A spent-development improvement does not enable production.

## Evidence record

Record full source SHA, protocol/input hashes, flags, relevant executable/runtime
identities, executed tests, per-family aggregates, paired fixes/breaks, support and
unknown counts, and memory/time scope. Keep private source-level evidence external.
Publish a valid rejection as carefully as a pass; state the exact evidence level.
