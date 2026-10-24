# R5 saved-generated diagnosis

Date: 2026-10-24. **DIAGNOSIS_COMPLETE_NO_REPAIR.**
Clean pushed audit source: `8dc8b7b8be1883250e11c0e69c5d6d439f071025`.
The [aggregate receipt](COUPLED-TIME-R5-AUDIT-20261024.json) binds exact source,
registration, runtime, private traces, supervision and output digests.
[Preparation](COUPLED-TIME-R5-AUDIT-PREP-20261024.md) and its nine generated tests
were committed/pushed before the audit. R4 stays operational PASS / generated
scientific REJECTED. No old candidate, score, counter or token was rewritten.

## Shared regression cause

The auditor inspects all100 saved sources and600 saved windows. Paired window,
source and cohort counters reproduce exactly. The one meter break and one
downbeat break belong to **the same source/window**, not two unrelated cases.
Beat counts and downbeat ordinal topology match in all100 sources. In the
failing window, original/candidate event membership also matches.

Exactly two label-blind canonical projector calls replay the original and
candidate saved tuples for this case. The old projection is accepted with a
meter. The candidate's fitted tempo is still within the unchanged supported
range, but its rounded OLS intercept falls before the window start. The frozen
projector's anchor-in-window guard returns `unsupported_projection` before the
bar/meter extraction code runs; the resulting empty meter causes both paired
regressions. This is a projection-contract boundary, not a missing beat or changed
downbeat ordinal. Absolute coordinates and source/window identities remain private.

The diagnostic reproduces existing behavior. It does not clamp or shift the
anchor, copy the old meter, weaken the range guard or alter the coupled solver.
Such changes would affect a registered scientific/representation contract.

## Support accounting blocker

The saved common diagnostics count600 windows with400 complete reference contexts;
200 lack complete bracketing under the unchanged predicate. The old scorer
emits no `reference_incomplete_windows` field. Consequently its Counter lookup
returns0, which is not evidence of zero unsupported windows. The public audit
uses null for the un-emitted serialized counter and reports the derived200
separately; it does not alter the old saved counters.

The frozen real registration requires401 complete /199 incomplete contexts.
Its evaluator checks the absent incomplete counter against199. This interface
mismatch is a static real-evaluation blocker. No real reference manifest was
opened to reach that conclusion, and real evaluation was not executed. A repair
must emit the intended context metadata and enforce conservation; replacing199
with0, dropping the check or rescoring old real partials is not a repair.

## Evidence and limits

| Item | Observed |
|---|---:|
| Saved sources/windows inspected | 100 /600 |
| Affected sources/windows | 1 /1 |
| New candidates /new scoring calls | 0 /0 |
| Canonical projection replays | 2 |
| Bound files rechecked | 615 |
| Saved R4 files among those bindings | 316 |
| Supervised audit wall | 19.236 s |
| Maximum observed process RSS/HWM | 31668 KiB |
| Private and aggregate audit output | 106690 bytes |
| Outer wall including immutable-image validation | 56.779 s |

The registered120s/262144KiB/4194304-byte audit caps pass with process-group
supervision. Reference label manifests, real corpus/audio, inference, training,
new candidate construction and all musical scoring functions remain unopened
or uncalled. The previous nine tests are exact-component preparation evidence;
they were not rerun during the audit. All25 R4 component hashes,13 immutable
scientific/evaluation/support files and15 registered documents remain unchanged.
The current R4 preflight token is preserved and new real authorization remains absent.
Historical authority documents are hash-checked only; none is activated or rewritten.

The actual historical **real neural-runtime freeze was not audited** by this
saved-generated tool. The known generated runtime library replacement does not
requalify that older real freeze; this remains an explicit later metadata gate.
No C/Rust/native/P4 matrix or physical qualification was run for this host audit.

## Next checkpoint

Follow the [ordered repair plan](COUPLED-TIME-R5-REPAIR-PLAN-20261024.md) and its
[machine checklist](COUPLED-TIME-R5-REPAIR-PLAN-20261024.json). First register the
support-counter-only extension with exact parity requirements, then implement
and qualify it in a separate checkpoint. Treat the projection-boundary contract
as a distinct scientific decision. Neither repair nor real authorization is
implemented by this audit or plan; VERSION and production behavior stay unchanged.

Retained private case traces, coordinates, identities and bindings are in the
WSL `coupled-time-r5-audit-20261024` output directory. Dispatch, stdout/stderr,
resource and publication evidence is retained outside Git under
`D:/AI/LIBAPTA/outputs/coupled-time-r5-audit-20261024-evidence/`.
