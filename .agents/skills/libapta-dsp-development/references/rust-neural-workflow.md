# Rust and neural DSP workflow

Read with the active plan linked from `docs/rust/MIGRATION.md`. Model selection,
algorithms, parameters and hashes belong in the registered experiment protocol;
this reference supplies reusable boundaries.

## Capture, construction and evaluation

- Capture preserves registered raw outputs and geometry. Verify shape, finite
  values, coverage, source/endpoint ownership and legacy event replay where
  required. Capture integrity is not musical accuracy.
- The pure constructor receives only permitted evidence and geometry. Do not
  pass labels, saved correctness verdicts, source IDs or reference-derived paths
  that could select a candidate.
- Isolate constructor imports and I/O from evaluation. Use separate processes
  when the protocol requires them; freeze every candidate/hash before loading
  labels. Verify generated-fixture output invariance with absent/changed labels.
  An import boundary/test is not an OS sandbox; describe the actual isolation.
- Bind checkpoint, preprocessing, resampling, hop, time origin, source rate,
  padding, overlap and future context. A row center is not causal availability.
  Missing context and exclusive-endpoint events remain explicit.
- Keep raw traces outside Git. Clipping, quantization or interpolation is a
  separately registered decoder operation; never rewrite the raw input artifact.
- Inspect canonical scorer input fields and use its unchanged semantics. Do not
  substitute seconds for source frames or nominal periods for unknown successors.
- Hash mismatch, timeout and incomplete cohorts are integrity failures. Preserve
  partial artifacts; do not report only the successful subset or overwrite a run.

## Transfer and training membership

Before describing transfer as independent, document:
1. Model-training, candidate-tuning and prior-selection membership, including
   unknown membership rather than assuming absence.
2. Recording/performance identity and duplicate/transcode checks. Different
   filenames or encoded-file hashes alone do not prove different recordings.
3. The exact development split and untouched formal holdout boundary.
4. Source/label provenance and rights/access for newly used data.

If independence cannot be established, report diagnostic-only or
INDEPENDENCE_UNRESOLVED. Public availability and dataset size do not establish
fresh acceptance. Do not open a formal holdout to replace missing development data.

## Rust implementation boundary

- Native/wire correctness, musical quality and consumer adoption are separate.
  Preserve no_std/caller-owned storage, checked capacity and bounded work where
  required by the active core contract.
- For a product-directed port, follow the accepted specified algorithm and its
  gates. An explicitly registered native experiment may precede musical acceptance
  but stays opt-in and diagnostic. Neither route enables production automatically.
- Verify numerical decisions, coordinates, ties, status and output parity against
  frozen fixtures. Test cancellation, budgets, capacity and output lifetime in
  the changed layer; feature-enabled host tests do not prove embedded execution.
- Respect current segment/event limits and consumer representations. Unsupported
  output needs an explicit status or separately specified adapter; do not silently
  increase capacity or reuse a rejected fit.
- Follow the active checkpoint plan. Report unresolved APIs, product budgets or
  representation decisions instead of inventing values. This does not require
  re-approval of already authorized routine work.

## Whole-pipeline resources

Before a substantial product-directed embedded port, account for weights,
preprocessing, activations, decoder scratch/traceback, stack, output banks and
their lifetimes. Distinguish internal RAM, PSRAM and DMA requirements at the actual
platform boundary. Document throughput, first/final result latency, cancellation
and audio/USB coexistence against real product constraints. Include full-length,
gap and context requirements. Unknown limits remain UNKNOWN.

Offline analysis can be appropriate for advance analysis if the product allows it;
that does not establish standalone P4 feasibility. Checkpoint size is not peak RAM,
a Python harness with multiple models is not isolated model RAM, and desktop time
is not P4 timing evidence.

Separate estimated/static capacity, host parity, target compile/link, boot and
physical exact-image qualification. Follow the established hardware runbook.
Do not change training/models, offload processing, activate a provider or switch
consumer pins merely to close an unresolved resource gate.

## Independent outcome fields

Report capture integrity, musical development, transfer, confidence calibration,
native/wire support, consumer adoption and physical P4 separately. Zero confidence
is not calibrated safety, a label-derived witness is not detector accuracy, and
native decoder parity alone is not an end-to-end neural implementation.
