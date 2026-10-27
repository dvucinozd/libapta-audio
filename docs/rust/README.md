# Native Rust documentation

Current handoff: **2026-10-27 — PAUSED_BY_OWNER_WAITING_FOR_BOARD**.
The owner requested documentation closeout, commit/push and continuation when
the **JC1060P470C_I_W_Y** arrives. RC-04A software preparation is complete;
RC-04B runtime prerequisites and physical measurements remain open.

## Read first

| Document | Authority |
|---|---|
| [Board-arrival handoff](BOARD-ARRIVAL-HANDOFF.md) | Pause, retained artifacts and ordered restart checklist |
| [PRODUCT-FIRST-STATUS.json](PRODUCT-FIRST-STATUS.json) | Live phase/progress, owner decisions, blockers and evidence references |
| [MIGRATION.md](MIGRATION.md) | Current handoff at the top; dated implementation history below |
| [Rust source README](../../rust/README.md) | Native API/build guidance and dated software checkpoints |
| [Project roadmap status](../status/APTA-ROADMAP-STATUS.md) | C/stable release boundary and separate Rust continuation |

## Product requirements and sequence

| Documents | Meaning |
|---|---|
| [Scope](PRODUCT-SCOPE.md) | Waveform/BPM/grid/meter first; at most 600 seconds; key/calibrated confidence deferred |
| [Budgets](PRODUCT-BUDGETS.md) | Approved 5 s / 60 s / 50 ms targets; physical memory allowances still unmeasured |
| [Grid contract](PRODUCT-GRID-CONTRACT.md) | Output, publication, admission and cancellation semantics |
| [Product-first plan](PRODUCT-FIRST-IMPLEMENTATION-PLAN-20261026.md) | Initial PF task definitions and dependencies |
| [RC continuation plan](PRODUCT-CONTINUATION-PLAN-20261027.md) | Initial RC-01–RC-10 sequence and checkpoint rules |
| [RC-04 software split](PRODUCT-RC04-SOFTWARE-SPLIT.md) | Accepted preparation/physical split; original A1/A2 handoff is historical |
| [Whole-route decision](PRODUCT-ROUTE-DECISION-20261027.md) | Source-bound owner/lifetime inventory and WAIT_EXTERNAL decision |
| [Full-graph readiness audit](PRODUCT-FULL-GRAPH-READINESS.md) | Missing complete graph, resource and integration capabilities |
| [Candidate binding](PRODUCT-COMPACT-BINDING.md), [resource oracles](PRODUCT-COMPACT-RESOURCE-ORACLES.md), [rights review](PRODUCT-COMPACT-RIGHTS-REVIEW.md) | Static/finite evidence and unresolved exact-model rights/provenance |

## Completed software preparation and retained evidence

| Slice | Evidence and limit |
|---|---|
| HS01 | [HMM result](PRODUCT-HOST-HMM-RESULT.md): synthetic host decoder only |
| HS02 | [Grid result](PRODUCT-HOST-GRID-RESULT.md): fixed path-to-events/grid/native/wire checks |
| HS03 | [Resampler result](PRODUCT-HOST-RESAMPLER-RESULT.md): bounded synthetic FIR checks |
| HS04 | [Frontend result](PRODUCT-HOST-FRONTEND-RESULT.md): synthetic frontend/FFT/features |
| HS05 | [Neural result](PRODUCT-HOST-NEURAL-RESULT.md): artificial small recurrent kernel; not the trained graph |
| HS06 / RC-02 | [Probability result](PRODUCT-HOST-PROBABILITY-RESULT.md): clean four-case synthetic bridge result |
| RC-04A-1 | [Telemetry V1](PRODUCT-TARGET-TELEMETRY-V1.md), [implementation receipt](PRODUCT-TARGET-TELEMETRY-IMPLEMENTATION.json): bounded offline integrity/parser checks |
| RC-04A-2 | [Fixed probe core](PRODUCT-RESOURCE-PROBE-CORE-IMPLEMENTATION.md): no_std arithmetic, quota/cancel and public-oracle software checks |
| RC-04A-3 | [Target package result](PRODUCT-TARGET-ADAPTER-PACKAGE.md), [machine receipt](PRODUCT-TARGET-ADAPTER-PACKAGE.json): five real ELF/map/disassembly variants, build/link audit only |
| RC-04B | [Prepared physical protocol](PRODUCT-TARGET-PROBE-PROTOCOL.md): NOT_EXECUTION_READY; exact-unit/runtime gates remain |

The [adapter registration](PRODUCT-TARGET-ADAPTER-PROTOCOL.md) and
[implementation checkpoint](PRODUCT-TARGET-ADAPTER-IMPLEMENTATION.md) retain their
original REGISTERED/clean-package-pending labels. The later package receipt
records completion of preparation. These labels describe different checkpoints,
not conflicting current statuses.

## Reading historical records

Frozen plans, registrations, results, JSON receipts, oracle assets and external
raw artifacts keep their original hashes and source revisions. A dated "next"
instruction belongs to that checkpoint. Follow live progress and the current
owner decision before executing any plan. `reviewed_source_sha256` in live
status describes its original source review; it is not a claim that maintained
README files still have those bytes.

Earlier S6, DD, CT, P, MC and H0/H1 records in this directory remain historical
development evidence with their recorded accepted/rejected/invalid boundaries.
They do not reopen spent corpora or authorize a new experiment. C/Rust parity,
synthetic numerical correctness, target link checks, device measurements,
musical accuracy and release qualification are separate claims.

`BUILD_ONLY_LINKCHECK` is not physical execution. Parser `VALID_COMPLETE` or
`VALID_WITH_UNKNOWN_METRICS` is not device qualification. BSS reservations are
not free/peak memory allowances. VERSION stays **1.0.1**; no provider/product or
release activation follows from any documentation update.
