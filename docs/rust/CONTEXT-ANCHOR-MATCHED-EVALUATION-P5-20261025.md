# P5: generated matched runner and operational preflight

Date: 2026-10-25. Branch: `rust-rewrite`. Status:
**P5_GENERATED_OPERATIONAL_PREFLIGHT_PASS**. This qualifies the generated host
runner, not the fixed C3 matched comparison or musical/product acceptance.
Preparation was registered on 2026-10-24 against baseline
`463a89aa8a692695134a1de6d2fb3e7860dd5a67`; tests bind the new file bytes on that
baseline. They are not presented as a clean-source C3 campaign.

The [P5 preparation protocol](CONTEXT-ANCHOR-MATCHED-EVALUATION-P5-PROTOCOL.md)
was written before implementation. The [preflight receipt](CONTEXT-ANCHOR-MATCHED-EVALUATION-P5-PREFLIGHT.json)
binds the exact components/runtime/scope; the [aggregate evidence](CONTEXT-ANCHOR-MATCHED-EVALUATION-P5-20261025.json)
records resources, preserved bindings, log hashes and preparation history.

## Implementation

The three remaining P3 implementation paths now provide a separate runner,
evaluator and public process tests. `prepare_runtime` derives a composite closure
from the unchanged C3 snapshot, SHA-bound source and actual loaded runtime
profiles. `audit_c3` checks the metadata chain and opaque leaf hashes; `stage`
creates minimal closed jobs. `dispatch` runs PROJECT, VERIFY and EVALUATE in
separate supervised processes. `run_public` accepts only the fixed public
arithmetic recipe; `run_registered` remains blocked without a separately tracked
P6 generated-scope proposal and matching preflight.

PROJECT uses the unchanged P1 projector, VERIFY the unchanged independent rational
oracle. Both deny scoring imports and restrict JSON reads to explicit inputs and
runtime descriptors; reference labels are withheld. All projections freeze and
independently replay before EVALUATE may load labels. EVALUATE first requires
exact frozen legacy diagnostic parity, then uses unchanged canonical scorers and
P4 support/ledger helpers. Raw event metrics and actual successor masks remain
separate from nominal projection cells. Source/group/cohort totals expose all,
complete and incomplete subtotals, including explicit zeros.

Closed records, strict JSON/types, full ordered manifests, source revision,
scope/runtime/component identities and upstream completion hashes reject partial,
stale, corrupted or mixed attempts. A failed attempt retains its first INVALID
marker and cannot resume. Limits remain 60 seconds/source, 1800 seconds/phase,
262144 KiB/process and 67108864 bytes/attempt including staged inputs, logs and
receipts. Source alarms and worker RSS/HWM checks supplement the parent supervisor.

## Verified evidence

All **39 tests pass**, with zero failures/errors/skips: 14 new P5 process tests,
15 unchanged P4 controls and 10 unchanged P3 controls. The final P5 suite takes
173.060 supervised seconds; P4/P3 controls take 2.812/1.347 seconds. Negative
checks cover absent/changed labels, incomplete verification, independently replayed
projection corruption after hashes are adjusted, source/order/type/JSON/runtime
corruption, legacy parity, closed attempts and timeout/RSS/output enforcement.
The context-support counterexample correctly rejects a shared correctness break
despite new support. This prevents support gains from becoming false fixes.

The full public fixture completes 100 sources / 600 windows, 400 complete / 200
incomplete contexts, 1200 projections, 2400 cells, 7416 ledgers and 103 canonical
source/group/cohort tables. The arithmetic arms are deliberately equal; its
`PAIR_NOT_REJECTED_ON_GENERATED_DIAGNOSTICS` is an operational check only.

| Phase | Supervised wall seconds | Worker self RSS/HWM KiB | Labels loaded |
| --- | ---: | ---: | --- |
| PROJECT | 6.889 | 24040 | false |
| VERIFY | 8.828 | 24060 | false |
| EVALUATE | 19.332 | 188172 | true |

The parent supervisor observes 113776 KiB in each phase. Namespace worker
self-measurements are reported separately, rather than treating the supervisor's
observed maximum as the worker peak. All are below the registered limit.
Attempt output is 18580429 bytes plus 5674981 public fixture bytes: 24255410
accounted bytes. The cap applies to each fresh attempt, not the sum of retained
preparations. Resource negative tests exercise the frozen common supervisor;
these do not constitute a stress qualification of actual C3 or P4 hardware.

The C3 metadata audit verifies **3716 bound files**, 100 sources / 600 windows,
in 64.556 seconds with 18372 KiB observed RSS. Label-free inventory/input metadata
is parsed for identities, ordering, geometry and paths. Reference/event/score
leaves are hashed opaquely; their contents are not parsed for scoring. No fixed
C3 matched evaluation ran.

Derived runtime SHA-256:
`19484314cdb245e1ca81197057a989afb7d2786df1f38f88f91e8d1679262e9c`.
Scope SHA-256:
`2e45d042ea7e8906fb8eb42c19623c491b913259c2be47f6d87240d5f65cefdf`.
The unchanged parent snapshot SHA-256 remains
`563f613b905fc95ef349101b8d83752ebf14e91dc016d8ed9b3e285e045a6f58`.
All non-source loaded bytes match that parent's inventory: PROJECT/VERIFY each
load 80 parent files plus 3 source files; EVALUATE loads 889 plus 20. There are
zero unbound loaded files. The namespace mounts 13 parent runtime roots and the
source read-only, disables bytecode caches and checks loaded bindings before/after
each phase. Superseded descriptors are retained with derivation records; the
parent image is not rewritten.

P3's 23 bindings, P0's 14 documents / 17 reviewed sources / 37 frozen C3
components, P2's 13 authorities and P4's exact receipt/components still verify.
`VERSION` remains `1.0.1`; constructors, projectors, scorers, old evidence and the
actual consumer provider/pin remain unchanged.

## Preparation history and remaining gates

Earlier public preparations retain six failures caused by an overly narrow
parent-snapshot JSON whitelist and one error from attempting to overwrite an
INVALID marker. Both were repaired before final qualification. Their logs and
superseded runtime descriptors remain external; they are not counted as final
passing executions. Passing shorter suites were supplemented by explicit zero
subtotals, worker self RSS and the embedded-parent identity guard. The final
14-test suite qualifies the final descriptor above.

Local aggregate artifacts are retained under
`D:\AI\LIBAPTA\outputs\context-anchor-matched-p5-20261024`; final public attempt
artifacts are under the WSL `context-matched-p5-public-preflight-verified-20261024`
directory. Receipts bind exact paths and hashes; no audio/private source mapping
is published.

**Next: P6, separately register one matched comparison on the fixed generated
C3 cohort.** Bind the exact scope, derived runtime, preflight, components and new
destination; commit/push that proposal and verify a clean matching remote SHA
before dispatch. Preserve the full result whether PASS, rejected or INVALID;
do not retry, retune or relax gates within that attempt. No P6 proposal is created
at P5.

C3's old scientific **REJECTED14/16** remains frozen. Real-runtime metadata is
**NOT_AUDITED**, independence **UNRESOLVED**, confidence unqualified and real
authorization absent. No real/GTZAN scoring, inference/training, native consumer
build, full C/Rust matrix, physical P4, formal acceptance or release ran here.
