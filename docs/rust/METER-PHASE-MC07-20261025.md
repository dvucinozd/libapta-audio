# MC-07: saved-format adapter and public operational preflight

Date: 2026-10-25. Preparation baseline `rust-rewrite`
`78f87882a9c548e6766285914fc5cbdaa4a89f8a`; measured implementation/test bytes
are bound by the [operational readiness receipt](METER-PHASE-MC07-PREFLIGHT.json).
Scope: newly authored public fixtures only. No saved MC-03 source case, projection,
reference or per-source evaluation is opened by this checkpoint.

The [MC-06 registration](METER-PHASE-MC06-AUDIT-PROTOCOL.md) remains immutable.
The two new source files are the [adapter](../../tools/apta_1_1_meter_phase_audit.py)
and [public tests](../../tests/unit/apta_1_1_meter_phase_audit.py). MC-05, canonical
P3 arithmetic, MC-00 and the previous runtime/source helpers remain unchanged.

## Behavior and boundaries

The adapter normalizes the complete saved MC-00 record into the five MC-05 window
fields and five normalized meter fields. It checks projection/job/proof identity,
closed fields, exact integer geometry, source/window counts, P1 witness retention,
unsupported/absent cases and the existing exact Q32 grid/point/meter contract.
It does not construct, refit, stitch or repair a projection. Public fixture
generation uses the unchanged MC-00 projector on arithmetic sources; that is
separate from the adapter, which invokes no projector or model.

EXTRACT runs without evaluation imports, freezes both arms' K records and their
ordered completion manifest, and denies opening the selected labels/evaluation
files. INTERPRET verifies that completion before loading references, evaluates
every phase point with MC-05 and requires original context-cell points, meter,
M/D and canonical meter/downbeat counters to match saved controls exactly.
Point arrays stay in external per-source outputs; cohort aggregation retains
compact window evidence. E has a distinct format and never enters the P3 screen.
M/D use unchanged canonical ledgers. Conditional correctness remains null for
empty E. Wrong meter is separate from all-supported-point phase correctness.

Existing helpers supply strict JSON, binding/read/write-new, source deadlines,
namespace construction, runtime inventory checks and process supervision. Both
phases run with the frozen parent interpreter/image, read-only source and inputs.
Input mounts precede runtime/source overlays, so an input directory cannot shadow
the frozen interpreter or libraries. Every phase worker applies a hard 256 MiB
address-space limit, stricter than the inherited 262144 KiB RSS ceiling. Source
deadlines, self RSS/HWM checks and the parent supervisor remain active. The AS
limit also constrains transient allocations when a bwrap-created session is not
visible to an outer process-group sampler; sampled peaks are not asserted to be
a complete worker-process census.
A Python open guard proves the stated label-read boundary; the inherited outer
filesystem is visible, so this is not a private-data filesystem sandbox.

The later saved-launch API additionally requires a bound readiness receipt,
exact MC-06 input inventory and historical source/order/proof roots, an absent
registered output path and clean launch source. It does not run during public
preflight. The future launch must record the actual post-commit source SHA and
matching remote verification; no self-referential revision is inserted here.

## Evidence

**MC07_PUBLIC_OPERATIONAL_PREFLIGHT_PASS**: final **22/22 tests**, zero
failures/errors/skips; **211 phase-record oracle comparisons**, **1053 ledger
oracle comparisons**, and **four negative resource checks**. No saved-data audit
is executed. Exact runtime descriptor SHA-256:
`3fb1343c8fb181684303d9e2e7af7320e393250591613009dcf519df003073e4`.

Final public suite wall time 598.987 s, self peak RSS 85464 KiB; supervised outer
wall time 607.745 s. EXTRACT/INTERPRET take 86.802 / 236.593 s, with worker self
RSS/HWM 77048 / 76892 KiB and parent-observed peaks 85336 / 85464 KiB. Both worker
completions report a 268435456-byte hard AS limit. These include file/proof/
namespace and test bookkeeping; they are not isolated DSP timing.

Runtime preparation verifies the entire immutable parent before/after both
profiles, and qualification closeout verifies it again after the public suite.
Every loaded file must match either its fixed source hash or the parent inventory.
EXTRACT binds 85 loaded files (7 source / 78 parent); INTERPRET binds 87
(9 source / 78 parent), with zero unbound files. Read-only qualification closeout
takes 178.192 s; the preparation tree is 40745034 bytes at that check, below
67108864 bytes, including earlier public failures and runs.
The actual saved launch still needs clean pushed source and its own bound launch
receipt before reading registered per-source data.

Reproduction uses the retained parent interpreter, existing `engine.namespace`
and `io.supervise`, `-B -X pycache_prefix=/libapta-no-cache`, with
`APTA_MC07_RUNTIME`, `APTA_MC07_OUTPUT` and `APTA_MC07_RESULT` pointing to new
external public-preflight destinations, then runs:

```sh
python tests/unit/apta_1_1_meter_phase_audit.py
```

The bound external `prepare-runtime.py`, `run-public-tests.py` and
`qualify-public.py` record the concrete namespace/supervision commands. These
orchestrate existing helpers and metadata checks, not another scoring stack.

Final test counts, runtime/profile/entrypoint hashes, resources, commands and
external evidence identities are in the linked machine receipt. The public suite
includes a 100-source/600-window/1200-projection pipeline with four groups,
945 M/D/E window ledgers and 630 per-arm subtotals. Its reference spans give
400 complete and 200 incomplete windows. Public saved-format grid counters are
structural controls, not grid-accuracy measurements. Full phase records and all
945 ledger partitions are compared to independent existing public oracles.

Checks cover both meters, absent/unsupported/model-empty data, malformed types
and geometry, changed points/witnesses, stale/swapped proofs, reference source
order, exact old M/D parity, frozen K, E nulls and unequal point/window counts,
all-points vs best-point traps, cross-grid non-equivalence, hierarchical count
conservation, output/input overlap, symlinks, source/runtime-profile changes,
read-only input writes, source deadlines and negative process resource cases.

An initial 11-test unit run passed. The first outer operational launch was closed
INVALID before test execution because a negative-test symlink from that unit run
remained inside its monitored output tree. The integrity rejection was correct.
The owned link's metadata and failure logs were preserved, only that test link
was removed, and the link-rejection fixture moved to a scoped temporary directory.
No MC-05/scoring/protocol repair or private-data attempt was involved.

The next 19-test public run passed (209 phase-oracle and 1053 ledger-oracle
comparisons). Final source review then added the hard worker AS limit, protected
mount ordering and INVALID closeout on later saved-audit closure failure. Added
checks cover fractional-Q32 saved-format round trips, actual allocation rejection
and production mount ordering. Those final bytes receive a new runtime descriptor
and complete public verification; earlier results are not reused as their proof.

Subsequent public runs and final review hardening are retained under
`D:\AI\LIBAPTA\outputs\meter-phase-mc07-20261025`. The 64 MiB ceiling accounts
for that whole preparation/test tree, including earlier partials, profiles and
logs. Old input/runtime images remain dependencies. Self process RSS, parent-
observed peaks, per-phase wall time and outer bookkeeping are separate measures;
none represents product RAM or physical P4 latency.

## Next checkpoint

MC-08: bind a concrete clean-source launch to this readiness receipt and the
unchanged MC-06 registration, then execute one saved-generated interpretation
audit in the registered fresh destination. Rehash exact input/runtime roots,
freeze all K before labels, preserve original M/D and the MC-03 rejection,
independently close output/ledger/conservation proofs and publish aggregates only.
On integrity/resource/partial-cohort failure preserve INVALID output and stop;
no automatic retry, subset run or in-attempt repair.

MC-03 remains rejected. Real-runtime H0 is NOT_AUDITED, independence UNRESOLVED,
confidence unqualified, whole-pipeline budgets UNKNOWN. No real/GTZAN scoring,
inference/training, consumer/native activation, C/Rust build, P4 measurement,
fresh acceptance or release occurs here. VERSION remains 1.0.1.
