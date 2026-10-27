# Resource telemetry V1: bounded offline interchange

Date: 2026-10-27. Scope: RC-04A-1 host integrity contract.
Implementation:[offline parser](../../tools/apta_target_probe_telemetry.py).
Tests:[integrity tests](../../tools/tests/test_apta_target_probe_telemetry.py).
Examples:[manifest](fixtures/target-probe-telemetry-rc04a/example-manifest.json),
[dedicated log](fixtures/target-probe-telemetry-rc04a/example.jsonl).
All example identities and readings are explicitly synthetic.

## Envelope and limits

An expected UTF-8 JSON manifest and dedicated UTF-8 JSONL log are separate inputs.
Every log line is exactly `M1_RESOURCE_PROBE_V1 ` followed by one JSON object and
LF or CRLF. A final newline is required. Boot/service messages belong in their
existing collector; noise is refused here. Each record has schema integer1.
Duplicate or unknown keys, nonfinite/floating JSON numbers, booleans used as
integers, malformed UTF-8 and noncanonical identity tokens/digests are refused.
Optional measurements use JSON null; fields themselves remain mandatory.

| Bound | Bytes/count |
|---|---:|
| Input log |1048576B|
| Expected manifest |131072B|
| One line including prefix/newline |8192B|
| Planned observations |1..1024|
| Retained output(raw+manifest+result) |2097152B|

Ingestion reads at most cap+1 for each input. An oversized input produces INVALID
and retains only that bounded prefix; explicit truncation flags and hashes refer
to retained bytes, never to an unread suffix. Output is a new exclusive directory;
existing evidence cannot be overwritten. No full parsed-frame duplication is
stored:raw bytes are retained with bounded per-observation summaries.

JSON integer tokens are at most21 characters; unsigned telemetry values are
0..2^64-1 unless a narrower domain below applies. JSON real/exponent notation
is refused even if it denotes an integer. Arithmetic comparisons use integer
operations, avoiding float conversion of64-bit counters.

## Manifest: exact fields

`schema`, `identity`, `clock_hz`, `counter_bits`, `observations` are required.
No additional fields are accepted.

Identity has exactly:

- `run_id`, `unit_id`, `image_id`:1..80 ASCII letters/digits/underscore/dot/colon/hyphen.
- `board`:literal `JC1060P470C_I_W_Y`.
- `elf_sha256`, `protocol_sha256`:64 lowercase hex characters.
- `source_revision`:40 lowercase hex characters.
- `chip_revision`:integer302..65535, explicit expected observed value; policy
  minimum is not a detector. A synthetic fixture302 is not a board observation.
- `evidence_kind`:literal `synthetic` or `device_log`. Parser acceptance never
  authenticates this declaration or changes it to physical qualification.

`clock_hz` is integer1000..1000000000; `counter_bits` is integer32 or64.
These are expected timer parameters that a later actual producer must bind.
No current clock/port is guessed from the example. Parser comparison does not
calibrate or physically verify the clock.

`observations` is a nonempty ordered list. Each entry has exactly `id`, `case`,
`variant`, `trial`, `phase`, `max_elapsed_ms`. IDs are unique; id/variant/phase
use the identity token alphabet/bound. Case is TP-00..TP-06; trial is integer
0..1024; max_elapsed_ms is integer1..300000. This describes the exact expected
capture subset, not a certificate that all physical protocol cases ran.
The later runner must freeze one complete attempt/case plan from the protocol,
including variants/warm-up/trials/samples, and audit cross-capture completeness.
The parser does not invent missing cases or silently shorten a cohort.

## Observation: exact fields and meanings

```text
schema, kind, sequence, identity, observation_id, case, variant, trial, phase,
status, ticks_start, ticks_end, elapsed_ticks, clock_hz, memory, stack, counters,
result_sha256
```

Kind is `observation`. Identity must exactly equal the expected manifest,
including evidence kind and field types. Sequence starts0 and increments once
per accepted observation; observation_id/case/variant/trial/phase must match
the next planned entry. No repeated, omitted, reordered or unplanned item passes.
Status is MEASURED,NOT_SUPPORTED,FAILED or CANCELLED; valid failure diagnostics
are retained and listed separately. The parser does not change them into PASS.

Start/end/elapsed ticks are unsigned values smaller than2^counter_bits.
Require `(start+elapsed) mod 2^counter_bits == end`, the exact expected clock,
and `elapsed_ticks*1000 <= max_elapsed_ms*clock_hz`. One counter wrap is
representable; an interval spanning a whole counter period cannot be encoded
unambiguously and is refused. Each planned maximum duration must also be strictly
shorter than one whole counter period. No elapsed seconds are guessed or rounded.

`memory` has exactly internal,psram,dma objects. Each has exactly
capacity_bytes,used_bytes,free_bytes,minimum_free_bytes,largest_free_block_bytes;
each metric is null or unsigned64-bit integer. For non-null comparable values:
metrics<=capacity, used+free==capacity, minimum/largest<=free. Define used as
capacity-free for that reported allocator/region, including its accounting
overhead; do not feed differently scoped counters into this relation. Partial
metrics are allowed and remain unknown. No free value is derived automatically.
DMA may overlap internal memory:the parser never sums classes into a total.
Actual allocator APIs, region disjointness and placement remain target work.

`stack` has exactly capacity_bytes and high_water_used_bytes, each null or u64;
known high-water must not exceed known capacity. `counters` has exactly
audio_underruns and usb_errors, each null or u64. Nonzero counters are surfaced
for later qualification, not hidden by a valid record. Nulls are counted as
unknowns. `result_sha256` is null or64 lowercase hex:its shape/identity is
checked, but the telemetry parser does not independently validate numeric output.
A later numeric verifier must compare with the frozen oracles for the case.

## Terminal, incomplete captures and result

After every expected observation, require one `end` record with exactly
schema,kind,sequence,identity,observations. Its sequence and observations count
must equal the expected count. Additional bytes/records after it are invalid.
An early end or wrong count is INVALID. EOF without end or an incomplete final
line is INCOMPLETE. A malformed complete line is INVALID; processing stops at
the first error and preserves the accepted prefix and last complete sequence.

Result statuses:

- VALID_COMPLETE:all declared observations and end are well formed; metrics
  are non-null. It may still contain FAILED/CANCELLED/NOT_SUPPORTED observations
  or nonzero device error counters; inspect the separate lists.
- VALID_WITH_UNKNOWN_METRICS:complete declared capture with one or more nulls.
- INCOMPLETE:missing terminal/truncated final line; prefix is diagnostic only.
- INVALID:bad manifest/record/identity/order/arithmetic/domain/cap.

Always output `qualification=NOT_QUALIFIED`, `experiment_verdict=NOT_EVALUATED`,
`physical_measurement_verified=false`, `programmed_image_verified=false`.
This layer checks declared identity consistency; it neither hashes the actual
ELF nor proves that those bytes were programmed, that telemetry is truthful or
that memory/latency/numerical/product criteria pass. Existing exact-image package
verification, future numeric scorer and physical/operator evidence remain required.

## CLI and example

From LIBAPTA root, choose a new output directory:

```text
python tools/apta_target_probe_telemetry.py --manifest docs/rust/fixtures/target-probe-telemetry-rc04a/example-manifest.json --log docs/rust/fixtures/target-probe-telemetry-rc04a/example.jsonl --output NEW_EXTERNAL_OUTPUT
python -B -m unittest discover -s tools/tests -p test_apta_target_probe_telemetry.py -v
```

Exit0 means VALID_COMPLETE or VALID_WITH_UNKNOWN_METRICS only. Exit2 retains
INVALID/INCOMPLETE input evidence. Exit1 is an I/O/operational refusal such as
an existing output directory. Read inputs before creating the new output; if an
input cannot be opened there is no invented partial capture. Retain manifest.json,
raw.log and result.json exclusively. No serial/flash/network/model options exist.
