# HS-03: bounded streaming CS-FIR65 resampler

Date: 2026-10-26. Baseline `7ef5500877df760633c016e6568d00ad1b325d95`,
branch `rust-rewrite`. [Machine registration](PRODUCT-HOST-RESAMPLER-PROTOCOL.json),
[public fixtures](fixtures/product-host-resampler-hs03/cases.json),
[independent reference](fixtures/product-host-resampler-hs03/resampler_reference.py).
Status: **REGISTERED_NOT_IMPLEMENTED**.

## Scope and hypothesis

The owner accepted registration, implementation and synthetic verification of the
specified 44100/48000Hz to22050Hz resampler. A65-sample ring and scalar F64 FIR
can preserve CS-01 timing/EOF and frozen CS-02 coefficients under arbitrary input,
output and work fragmentation, without retaining whole-song PCM or allocating
in the component. This is a separate opt-in host addendum, not product PF-04.
Do not run weights/models, music, labels, formal holdouts, training, frontend/FFT,
consumer/device/provider activation or release. Model rights/provenance and PF-01
physical budgets remain open; board unavailable, silicon revision unobserved.
VERSION1.0.1, core/default/wire and HS-01/02 remain byte-identical. No dependency
is added. New runtime feature `experimental-compact-resampler=[]` gates the module
and its fixed synthetic CLI; it does not enable the other experiments.

## Arithmetic and source contract

Known source length N, fs44100/48000, channels1/2, N<=600*fs. N=0 is diagnostic
NoEvidence with zero outputs; the product profile still requires N>0. Unsupported
rate/channels/duration is UnsupportedProfile before arithmetic. Input is finite,
already normalized f32 PCM in[-1,1]. Reject violations, do not clamp or renormalize.
Stereo mono is exactly f32 add(L,R), then f32 multiply by0.5. Every real source
frame is validated, including samples read solely for future context.

K=ceil(N*22050/fs), at most13230000 output samples. For output index k0..K-1:

```text
fs44100: center=2*k, phase=0, phase_count=1
fs48000: center=(320*k)//147, phase=(320*k)%147, phase_count=147
for tap0..64, j=center+tap-32:
    x=mono_source[j] when 0<=j<N, otherwise +0.0
    product=f64(x)*frozen_coefficient[phase,tap]
    accumulator=accumulator+product           # initial +0.0; ascending taps
y[k]=round_to_f32(accumulator)
```

Use the normalized little-endian F64 tables from CS-02, exactly520/76440 bytes,
total76960 bytes. No coefficient regeneration, f32 reinterpretation, FMA,
reassociation, tap skipping, output clipping or edge renormalization. Tables
already include the full65-tap denominator even at boundaries. Nonfinite output
is an explicit arithmetic failure. Output can exceed[-1,1] due to FIR ringing;
future frontend amplitude/tolerance registration must account for that envelope.
This is the specified preprocessing change, not parity with soxr/training input.

Output time is k/22050 seconds, source position is center+phase/phase_count.
No group-delay timestamp shift. Availability requires min(N,center+33) source
frames consumed, distinct from the output timestamp. Position queries at k>=K
return None. EOF is exclusive: produce exactly K, never an extra tail sample.

## Streaming API, lifetimes and work

Safe Rust, no I/O/callback in the component:

```text
Config { source_rate:u32, channels:u8, frames:u64 }
InputBlock { first_frame:u64, interleaved:&[f32] }
Session::new(config) -> Result<Session,Error>
step(quota:usize,input:InputBlock,output:&mut[f32],cancel:&AtomicBool) -> StepReport
finish_input() -> Result<(),Error>
status(); phase(); progress(); source_position(k); cancel()
StepReport { consumed_frames, produced, first_output, units, flow, status, error }
Status = Running | Done | NoEvidence | Cancelled | Failed
Phase = Fill | Convolve | Emit | Terminal
Flow = Budget | NeedInput | NeedOutput | Terminal
```

Session owns only a fixed[65]f32 ring (260 bytes), F64 accumulator and scalar
counters/diagnostics. Tables are borrowed static bytes, never copied/allocated.
Constructor/step/cancel/finish/position/progress allocate no heap. No input/output
slice or caller pointer survives step. Report source counts in FRAMES, not scalar
interleaved values; first_output is the global k before this call. Caller advances
only by reported consumed_frames, keeps the unconsumed tail, and owns emitted PCM.

Each unit ingests/validates/mixes one frame, makes one phase transition, performs
one FIR tap or atomically emits one completed f32 sample and advances its counters.
Partial accumulators stay private. Ring ingestion stops at min(N,center+33), so
the live window fits65 samples even if the caller offers a larger block. Do not
consume future blocks to fill an output sink that has no space. Input/output
buffers in the long harness are at most512 source frames and256 outputs, reused;
the user-facing API has no full-source allocation requirement or artificial chunk
size restriction. Work quotas1/7/256/4096/usize::MAX must give bit-identical output.

Priority: quota0 returns ZeroBudget, zero progress, unchanged state. Then terminal
states return zero/Terminal without reading buffers. A pending flag cancels a
running session before buffer access. Then check first_frame equals total consumed,
input length divisible by channels and offered frames<=N-total_consumed; errors
set Failed before writes. During work check cancellation before every unit, then
output space before ingest/convolution. Empty/exhausted input yields NeedInput only
when another real frame is required; empty/full output yields NeedOutput. A call
may return flow with positive progress already made. No busy loop for unavailable
buffers; budget exhaustion yields Budget. Bad PCM is not consumed; report all prior
progress/writes in the same call with error and terminal Failed.

Errors: UnsupportedProfile, InvalidInput, InvalidPcm, SourceOffset, SourceLength,
TruncatedSource, ArithmeticOverflow, ZeroBudget. ZeroBudget is the sole nonterminal
step error. All errors/progress are reported explicitly, never a silent EOF/gap or
invented silence. finish_input while Running with fewer than N consumed frames
sets Failed/TruncatedSource. After N it succeeds and remaining outputs can drain
using empty input with first_frame=N. It never resumes a terminal session.

Done requires both K emitted and N consumed. cancel changes Running only, records
the phase where noticed, hides any pending sample and cannot resume after the
flag clears. Completed counts/status stay stable. PCM already streamed before
cancel/error is provisional working data; it is not a committed whole-source
analysis. No accepted product result/slot is written by this experiment.

## Oracles, preflight and controls

The fixed public JSON defines102 valid short cases: both rates/channels, empty,
one/two-sample and support/lookahead boundaries, DC/interior, impulses at0/32/
middle/EOF, alternating/step/negative-zero signals,147-phase/320-source-cycle
boundaries and4097-frame ring wrapping; stereo additionally covers cancellation
and left/right-only impulses. The reference uses direct indexed convolution,
not ring scheduling. Its math.fsum variant must meet the existing CS-02 bound
`abs(error)<=2e-5+2e-5*abs(reference)`; the initial ordered F64 prototype additionally
matches the registered f32 output bit digest exactly. There is no new tolerance.

Short public oracles are frozen before component code. Across all102 cases require
all input chunks1/7/256/512, output capacities1/7/256 and quotas1/7/256/4096/MAX
(60 schedules/case) to preserve count, input/output hashes and statuses. Compare
position/availability rationally, including last fractional source position.
Test zero output/input backpressure, partial-call progress, output canaries,
mid-tap privacy, exact65 ring capacity, source offset repeats/gaps, malformed
interleaving, oversupply, explicit short EOF, unsupported geometry/duration and
NaN/+Inf/-Inf/>1/<-1 in each stereo channel and after an already emitted prefix.
Test cancellation in Fill/Convolve/Emit, non-resumption, stable completed state,
fresh-session reuse and all constructor/error allocation-free code paths.

Long cases are a fixed257-frame periodic signal, exact power-of-two f32 values.
The independent full oracle hashes an infinite-periodic interior tile (257 output
samples at44100,37779 at48000) plus direct zero-padded first/last boundaries.
Prove this compression agrees with direct4097-frame reference at both rates and
channels before freezing; do not allocate full long PCM/output. Full input hash
is u32le fs,u8 channels,u64le N, then interleaved f32le source bytes; output digest
is emitted f32le bytes only. Fixture names/expected verdicts never enter Session.

The reference and CLI may allocate bounded oracle/generator/report buffers outside
Session. Record type sizes, ring/table sizes, actual input/output buffer capacities,
source/output counts and MAC units, construction/step/max-step/worker/CPU/RSS times,
first-output availability, observed output extrema and all hashes. No raw full
PCM or output traces are written. The output extrema are diagnostics, not clipping
or musical gates. Native PCM waveform/mixing controls and default runtime context
controls must remain green; the existing C-oracle-dependent ignored test remains
NOT_RUN without its actual oracle. fmt, focused Clippy(-D warnings), new debug/
release tests and release executable build are required, no unrelated C/P4 matrix.

## Fixed measurement and resource protocol

Order: fixtures, full44100-mono, full44100-stereo, full48000-mono,
full48000-stereo, cancel. Each full case is600 seconds, periodic source as fixed
in JSON, exactly13230000 outputs/859950000 MAC units. No new signal/seed/filter
selection. Cancel uses full48000-stereo, a separate thread requests after10ms while
the caller drives bounded steps; time request-to-step-return<=50ms, no final output
certificate, no resume. Record actual phase, provisional counts and cleanup/drop
separately. This is an asynchronous HOST resampler fence, not a P4 product target.

Known live component data are260 ring bytes plus scalar state; tables76960B,
stream buffers at most4096+1024B, SHA controls/report owners separate. Python's
observed runtime baseline is about19MiB; full oracle tile at most151116B before
Python controls. Use host per-case CPU60s, wall90s, RSS64MiB, AS128MiB and aggregate
attempt output1MiB. This allows the registered859950000 scalar MAC workload and
bounded runtime overhead, not a full song buffer. These are operational caps,
not physical RAM allowances or a promise of60s P4 completion.

Reuse unchanged neutral `apta_dense_runner_support.py` for source checks, bindings,
loaded source/cache/interpreter/library identity, process-group supervision and
exclusive external output. Preflight refuses wall0.05s vs1s sleep, RSS16MiB vs64MiB
touching allocation (verify allocating child),4096B vs65536B output and AS128MiB
vs256MiB allocation. Preserve all logs/partial files. Bind clean implementation,
protocol/fixtures/reference/coefficients/authorities, six code components,
binary/features/toolchain and runtime; recheck after each case and at finish.
CLI accepts fixed case names only, no audio/model/file-input path. Build/binary/
output stay outside checkout; output new and disjoint. Check supervisor final
status, inherited CPU/AS and child HWM as well as sampled RSS and aggregate bytes.
Input/API isolation and resource supervision are not an OS filesystem sandbox.

PASS is **HS03_SYNTHETIC_HOST_PASS_ONLY** only when every fixture/schedule/count/
bit/tolerance/position/status/cancel/no-regression/resource/identity check completes.
Semantic mismatch is synthetic FAIL; integrity/resource/incomplete run is
INVALID/INCOMPLETE. Stop and retain failure; no cap/tolerance/filter rescue, subset
or overwrite. An implementation repair requires a disclosed new clean checkpoint
before another attempt, never a real-data retry or alteration of frozen oracles.

Commit/push registration first. Implement only six planned code paths; tests/lints
and implementation commit/push precede the clean six-case run. Publish a separate
audited aggregate result and commit/push before the next component. This checkpoint
contains public mathematical oracle calculation only: candidate/tests/measurement,
full frontend/neural/native product, musical and physical qualification NOT_RUN.
