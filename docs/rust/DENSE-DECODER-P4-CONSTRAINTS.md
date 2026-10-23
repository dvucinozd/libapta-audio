# Dense decoder DD-00: verified inputs and P4 constraints

Date: 2026-10-23. Audit source:
`39d32ac225073e3b0f135ba14bb77738ce02e128`, clean `rust-rewrite`.
Scope follows DD-00 of the
[implementation plan](DENSE-DECODER-IMPLEMENTATION-PLAN-20261023.md).
[Machine-readable evidence](DENSE-DECODER-DD00-20261023.json).

## Decision

**DD-00 input integrity PASS. Proceed to DD-01 registration.**
The model/decoder product resource budget remains **UNKNOWN** and physical P4
execution remains **UNMEASURED**. Those gates block a product-directed Rust/P4
port, not the planned bounded host experiment. No decoder, scoring or new
inference is performed in this checkpoint.

## Input verification

All 300 private payload/metadata/legacy-event files match their bound SHA-256:
100 complete f32 binaries, 100 capture records and 100 legacy event records.
Binary decode/re-encode preserves exact bytes; metadata/payload identity,
source/resampled length, source/S6/endpoint partition, aggregation coverage,
statistics and all saved legacy event dictionaries reproduce.

| Verified aggregate | Count |
|---|---:|
| Sources / original S6 windows | 100 / 600 |
| Two-channel rows | 150095 |
| Source-owned / exclusive-end / beyond-source rows | 149997 / 98 / 0 |
| In-domain legacy beat / downbeat events | 6095 / 1851 |
| Separate endpoint beat / downbeat events | 10 / 3 |
| Binary payload bytes | 1202360 |

The saved dense report, legacy worker, capture protocol and plan identities
match the registered plan. Prepared-label manifest and old scoring report are
hash-bound only; neither is parsed. No audio, model forward, new candidate,
labels or old verdicts are used to construct a result. Current tracked source
hashes are checked before/after the audit. This is trace verification, not a
new musical evaluation or independent model inference.

Audit wall time: 25.036 s, including source hashing and final rechecks.
One preliminary audit reached the consumer clean-tree check before any report
was written: WSL's default Git filters interpreted the Windows consumer CRLF
checkout as dirty. The final audit uses `git -c core.autocrlf=true` only for
that read-only consumer inspection, matching its Windows clean semantics.
No repository configuration/files were normalized or changed.

External reproducible command, run from the Libapta repository:

```powershell
wsl -d Ubuntu -u daniel --exec python3 /mnt/d/AI/LIBAPTA/outputs/dense-decoder-dd00-20261023/audit_inputs.py
```

The script refuses to overwrite its evidence files. Its initial execution and
the clean successful audit are input checks, not attempts at the DD-05 decoder
experiment. New invocation needs new report destinations; do not overwrite them.
External evidence:
`D:/AI/LIBAPTA/outputs/dense-decoder-dd00-20261023/`.
Full private audit keeps bindings; the public JSON contains only aggregates,
source-document hashes and evidence identities.

## Consumer facts at the inspected revision

Read-only consumer: `D:/AI/M1_PCB`, clean `main`,
`0f2fb1425ef76f2541a8c76841cfa52178dca589`.
Its APTA dependency remains pinned to
`25d09de364e9507532c0508c838a8fecf0772d13`.
No consumer branch, pin, source, firmware or provider is modified.

Pinned sources:
- [Product requirements](https://github.com/dvucinozd/Pajoniiir-M1/blob/0f2fb1425ef76f2541a8c76841cfa52178dca589/docs/M1_RUST_PRODUCT_REQUIREMENTS.md):
  Guition JC1060P470C_I_W_Y / JC8012P4A1C_I_W_Y, P4 v3.2; actual unit package,
  memory and revision must be recorded. Rust target
  `riscv32imafc-unknown-none-elf`. P1-001 defers APTA production adoption until
  upstream embedded/release gates pass.
- [Architecture](https://github.com/dvucinozd/Pajoniiir-M1/blob/0f2fb1425ef76f2541a8c76841cfa52178dca589/docs/M1_RUST_ARCHITECTURE.md):
  native analysis is cancelled during playback; immutable accepted analysis and
  source/load generations are protected. Optional APTA is not activated on Guition.
  A 64-KiB CPU-cached PSRAM probe at 80 MHz is only a probe contract, not a
  whole-memory/allocator/DMA qualification or free-memory budget.
- [Adapter README](https://github.com/dvucinozd/Pajoniiir-M1/blob/0f2fb1425ef76f2541a8c76841cfa52178dca589/firmware-rust/crates/pajoniiir-apta-adapter/README.md):
  workers serialize jobs; service retains two output banks. Conversion must run
  outside audio/deadline-sensitive callbacks. Continuous final grid and matching
  full-source meter requirements remain; no hidden fallback from unsupported
  global data. The feature supplies no firmware task startup or bank allocation.

Current native policy is thus a background/stopped-deck path with cancellation
during playback. A future neural implementation cannot assume permission to run
through playback or to replace accepted analysis. Advance analysis may fit this
policy conceptually; its latency and whole-pipeline cost are unqualified.

## Resource inventory for DD-07

UNKNOWN means no complete product allocation/latency budget was found in the
inspected source set; it does not mean the hardware has zero capacity.

| Resource or behavior | Product limit | Existing need/evidence | DD-00 status |
|---|---|---|---|
| Internal RAM available to neural analysis | UNKNOWN | Audio/USB/display/executor allocations must coexist; no residual allocation approved | OPEN |
| PSRAM available and placement | UNKNOWN per unit/allocation | Board profiles and limited probe are distinct from usable neural allocation | OPEN |
| Stack and futures | UNKNOWN total | Adapter payload planning excludes owner/future/channel/stack overhead | OPEN |
| Weights / storage / inference engine | UNKNOWN allocation | Beat This! remains offline reference; no accepted native P4 backend | OPEN |
| FFT/features/activations | UNKNOWN | No isolated P4 peak measurement or full lifetime overlap inventory | OPEN |
| Dense v1 score/parent scratch | UNKNOWN placement | Plan derives at most 3566376 bytes for one m=4 table; no implementation measured | DERIVED ONLY |
| Existing local-tempo arrays | Caller ceiling, not production allocation | README: 113772 bytes for 320000 frames/FPC256/queue17/128 beats, including two output banks | DIAGNOSTIC ONLY |
| Existing service arrays | Caller ceiling, not production allocation | Above plus 4096-byte broker buffer =117868; excludes owned/pending copies and other product use | DIAGNOSTIC ONLY |
| Neural output/pending banks | UNKNOWN | Existing two-bank policy does not budget new network/decoder data | OPEN |
| Cancellation granularity | UNKNOWN neural time bound | Existing owner supports cancel/recovery; no bound for neural inference or dense DP yet | OPEN |
| First / useful / final result latency | UNKNOWN | No numeric neural-analysis deadline specified in inspected requirements | OPEN |
| Processing seconds per minute of audio | UNKNOWN | Host capture of 30-s prefixes is not a whole-track/device throughput guarantee | OPEN |
| Background CPU during playback | Current policy cancels native analysis | No neural/audio coexistence measurement; retain cancellation | POLICY KNOWN / PHYSICAL OPEN |
| USB/audio interference | No regression required; numeric neural budget UNKNOWN | Deadline path forbids I/O/unbounded allocation; integrated physical evidence absent | OPEN |
| Full-length / gap / changing-meter support | Product policy to be specified | Dense v1 host plan caps 30 s, rejects known gaps, fixes source meter to 3 or 4 | OUTSIDE V1 |

The capture's 648132-KiB harness RSS included Python/Torch and both loaded models.
It is neither Beat This!-only RAM nor a standalone-device requirement. The
431.529-s capture duration is also a host measurement with its stated exclusions.
Do not convert either into a P4 PASS or declare a P4 impossibility from it alone.

## Next checkpoint

DD-01: copy the proposed constants/gates into the registered protocol, bind the
plan and this verified trace set, and record the complete deterministic algorithm.
Commit/push registration before DD-02 code. No fresh corpus, labels, inference,
model change, default DSP, wire capacity, consumer activation or release follows
from this inventory.
