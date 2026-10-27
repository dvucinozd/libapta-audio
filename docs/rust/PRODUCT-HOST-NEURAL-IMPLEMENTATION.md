# HS-05 artificial recurrent implementation checkpoint

Date: 2026-10-27. Registration/source baseline:
`e16fa435c10baef83ebb0fb86bf67f5fcc43047b`.
[Protocol](PRODUCT-HOST-NEURAL-PROTOCOL.md),
[machine receipt](PRODUCT-HOST-NEURAL-IMPLEMENTATION.json).
Status: **IMPLEMENTATION_PREFLIGHT_PASS; CLEAN_MEASURED_RUN_PENDING**.

The opt-in `experimental-compact-neural` feature adds the registered toy
linear3->2,two-layer LSTM2,head3/stable-softmax state machine. It borrows the
four frozen113-parameter profile assets. Every row executes76 ordered affine
MACs and172 bounded units,including zero profiles. Both biases follow their
separate input/recurrent f32 dots;no FMA/F64 reduction,extra activation,full
product graph,trained checkpoint or new crate dependency is introduced.

Session owns64 f32 array cells/256B plus controls. The host value is352B,
alignment8;ProbeSession512B and long Driver1464B. All are below the registered
4096B component bounds. Static profiles1808B,driver input768B/output12B,
snapshot32B and51B packed transport record remain separate. These are type/
capacity facts,not actual host/P4 stack peaks or whole-pipeline memory evidence.
No heap allocation,I/O,callbacks or retained caller slices occur in Session/
ProbeSession. Scalar math functions are fixed operations with their actual
bounded-call elapsed time recorded by the host driver,not a target WCET proof.

Each input row is validated atomically. Incomplete affine/gate/cell/head/
softmax work stays private across quotas and starvation. Only Commit changes
the8-value visible state and emits a whole3-probability row. Reset is a separate
charged32B action at a drained row boundary;global indices/history counters
remain intact. Busy reset is inert,cancel has priority,terminal sources never
resume,and work-cap refusal precedes writes. Earlier rows remain provisional;
there is no product publication or accepted analysis slot.

ProbeSession exposes only fixed input fixtures and no partial result. It reuses
the same scalar helpers/engine. Large/nonassociative primitive inputs do not
widen Session's[-4,4] domain. Expected traces/bit arrays are isolated in the test
component and Python worker;candidate code contains only public inputs/weights.
Probe input bit decoding occurs at runtime to preserve the crate's Rust1.81 MSRV.

Executed checks:

- Five integration tests and two internal tests pass in debug and release,
  zero ignored. All34 probes use five quotas (170 schedules/build);12 sequences
  use40 schedules (480/build),including resets. Candidate bytes are invariant
  across its own schedules;exact affine intermediates,numeric nonlinear/state/
  probability oracles and stable argmax pass. Internal tests exercise partial
  second-layer recurrence,pre-write cap refusal,pending reset cancellation and
  leading-zero softmax exponent accumulation.
- Error/backpressure tests cover nonfinite/out-of-range columns0/1/2 after a
  provisional prefix,offset/length/EOF,output canaries,zero quota,empty input/
  output,legal repeated reset,busy/terminal reset,all phase fences,fresh sessions
  and CLI source/model/descriptor rejection. Snapshot visibility remains at the
  committed prefix while the next row is partial. No allocator-hook claim.
- Unchanged release controls:HS04 seven integration plus one internal test,
  HS03 six integration tests/6120 schedules,product-grid12 and default-context5
  pass. One C-context-oracle-dependent test is ignored/NOT_RUN. Prior full
  measured experiments and unrelated C/P4 matrices are not rerun.
- fmt,focused Clippy(-D warnings),release CLI build and Python compilation pass.
  Static registration validation confirms40 old authorities,135 prior assets,
  all28 new assets and the independent reference checks unchanged. The checker
  accepts3 valid transports and refuses13 malformed/exact-bit/numeric/ordering/
  JSON/trailer variants. All four operational violation probes are refused.
- Independent development fixture worker verifies46 observations (34 probes,
  12 sequences/90 rows),including every committed8-value state and terminal
  snapshot. Its bounded pipe contains125 complete records/15910 bytes;receipt
  preservation retains exact trailer bytes and hashes,not a full tensor spool.

During preflight the probe test exposed a too-early positivity check on softmax's
running sum:leading exponent terms can underflow to zero. The sum is now required
positive only after all three terms,as registered. Clippy also rejected const
f32::from_bits for Rust1.81;decoding fixed input bits at runtime preserves support
without changing input bytes. Both repairs precede source freeze/measurement;
no oracle,tolerance,parameter,algorithm,cap or backend was changed.

External development evidence:
`D:/AI/LIBAPTA/outputs/product-host-neural-preflight-20261027/`.
No long candidate case ran before this checkpoint. Commit/push implementation,
then run exactly the five fixed cases from clean exact source with a new external
release binary/output:

```text
python tools/apta_compact_neural_synthetic.py --executable BUILD/apta-compact-neural-synthetic --output NEW_EXTERNAL_OUTPUT
```

The supervisor binds actual compiler/Cargo/features,all source/reference/assets,
executable/linked libraries and Python/runtime files before cases. It checks
each worker runtime was prebound,then rechecks those bindings,source and caps.
CPU30s combined worker+waited child,wall45s,RSS64MiB/AS128MiB and1MiB aggregate
retained-output gates remain fixed. Preserve/stop on failure;no subset,overwrite,
cap/tolerance/backend rescue. Publish a separate audited result/handoff commit.

VERSION1.0.1,default/core/wire/consumer and HS01-04 sources remain unchanged.
This is artificial numerical host implementation only. Model rights/training
independence,full-size graph parity,PF01 actual Guition budgets and product PF04
remain open. No trained model,music,training,device,provider or release ran.
