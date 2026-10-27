# RC-04: software preparation and physical execution

Date: 2026-10-27. Input source `a6fcfa2390291d23b417ce41e995d4f7813d9824`.
User accepted preparation without the board:report format/parser/tests first,
then fixed probe arithmetic and target compile preparation.

This addendum supersedes the ordering restriction that blocked all RC-04 coding
on physical prerequisites. The original continuation plan, RC-03 snapshots and
prepared physical protocol remain unchanged historical authorities. Their
physical/rights/numerical gates are preserved. Current progress is recorded in
`PRODUCT-FIRST-STATUS.json`; no physical PASS follows from this addendum.

| Step | Work | Ready without board | Completion criterion |
|---|---|---|---|
| RC-04A-1 | Neutral telemetry format,offline parser,malformed/unknown/identity tests | Yes | Host contract and CLI tests pass; dedicated synthetic example; receipt/push |
| RC-04A-2 | HAL-independent fixed copy/math probe core and independent oracles from TP-02/03/04 | Yes,after RC-04A-1 | Register exact paths/backend/oracles/bounds before code; primitive,quota,cancel/capacity tests; separate freeze/push |
| RC-04A-3 | Opt-in target adapter,compile/link/map and capture-package preparation | Partly | Resolve selected timer/math/dependencies and single memory-owner interfaces from actual source; target build evidence only; unknown hardware bindings stay guarded |
| RC-04B | Physical identity,board/recovery,memory/resident/coexistence execution | No | Exact-board/image protocol prerequisites and registered measurements pass; complete PF01 evidence and justified allowances |

Parent RC-04 stays incomplete until RC-04B closes its required evidence. RC-05
may still record WAIT; RC-06/full model remains gated by budgets and rights.
Do not bypass a Guition compile guard or activate a provider to finish RC-04A.

## Current implementation boundary: RC-04A-1

`tools/apta_target_probe_telemetry.py` is a standalone stdlib host contract/parser
in LIBAPTA. It validates an explicit expected manifest and dedicated resource
JSONL log. It does not open serial ports, flash, run commands from logs/manifests,
import a model or allocate device memory. Its [format](PRODUCT-TARGET-TELEMETRY-V1.md)
defines future producer/consumer interoperability.

The consumer first-boot tool already provides board/image/minimum-revision/hash
and bounded-log concepts. Reuse those semantics here. Do not import/copy its
boot parser: it accepts repeated immutable boot records and distinct schemas,
where resource trials require strict sequence/completion and explicit unknown
live metrics. A future consumer collector should call/reuse this resource parser
as a canonical library or relocate it with import tests; do not implement a second
divergent interpretation. That wiring and live capture are later work.

Consumer `D:/AI/M1_PCB` has unrelated local files and an active managed worktree.
Neither was modified by RC-04A-1. Before RC-04A-2/3 code chooses a consumer path,
inspect attached worktrees and use a suitable isolated checkout; avoid adding
HAL-independent test work to a stale main checkout. If the math core remains in
LIBAPTA, register a no_std-compatible location and explicit later integration
rather than silently claiming consumer adoption.

## Next implementation handoff

On the next continuation, start RC-04A-2 registration/implementation according
to the physical protocol's fixed artificial inputs. Freeze source paths, arithmetic
and oracle identities, bounded units and cancellation before measured candidate
execution. Missing target math/backend/ownership choices block only their dependent
adapter work; report them while completing independent pure-core work. Preserve
all HS01–HS06 sources/results/default behavior. No new musical corpus or trained
weights are needed for this software preparation.

RC-04A-1 tests use synthetic manifests/records only. A log can match expected
identity while the actual flashed bytes or readings are unverified. Format integrity,
numeric correctness, physical memory/timing and product qualification remain
separate. `VALID_COMPLETE` is a parser result, never a device or experiment PASS.
