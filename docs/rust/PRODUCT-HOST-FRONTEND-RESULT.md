# HS-04 bounded frontend synthetic host result

Date: 2026-10-27. Verdict: **HS04_SYNTHETIC_HOST_PASS_ONLY**.
[Protocol](PRODUCT-HOST-FRONTEND-PROTOCOL.md),
[count erratum](PRODUCT-HOST-FRONTEND-COUNT-ERRATUM.md),
[implementation](PRODUCT-HOST-FRONTEND-IMPLEMENTATION.md),
[machine result and artifact manifest](PRODUCT-HOST-FRONTEND-RESULT.json).

The six registered cases ran once in order from clean implementation commit
`ee2a488ee8a309b7b8ba92407a5e648e2f79d91b`, already pushed before measurement.
The release executable was copied to a new exclusive external binary directory;
its hash, six source components, original protocol/fixtures/assets/authorities,
count erratum, actual Rust/Cargo tools, feature and loaded runtime files were
bound before the run. No candidate repair, retry, cap/tolerance/backend change
or successful-subset selection occurred during this measured attempt.

| Case | Validated observations | Feature rows | Worker wall seconds | Combined worker + child CPU seconds |
|---|---:|---:|---:|---:|
| fixtures |30 (10 primitives,20 streams)|54|0.662|0.217|
| fullzero-44100 |1|30000|21.604|27.328|
| fulldc2-44100 |1|30000|21.701|27.814|
| fullpulse441-48000 |1|30000|21.682|27.455|
| fullalternating2-48000 |1|30000|21.616|27.227|
| cancel |1|14 provisional|0.187|0.083|

All full cases consumed13230000 resampled samples and emitted30000 rows. Every
row executed42336 complex MACs:1270080000 per full case and2147940000 bounded
work units, below the registered4-billion limit. No zero/DC/periodic shortcuts.
Every emitted scalar was checked against both registered references. The four
complete feature digests also happen to match their dtype-aware reference
digests exactly; acceptance remains based on the unchanged numeric gates, not
a newly imposed FFT-vs-DFT bit identity requirement.

Actual Session.step time totals for full cases were20.561–20.646s; generated
input/hash/driver and pipe costs are separate. CLI pipe writes totaled0.706–
0.758s; the independent checker also records its own comparison and wait time.
Worker wall includes child/transport/checking. The largest observed bounded-step
elapsed time was18.517315ms; it includes host scheduling and is not a physical
P4 worst-case bound. Concurrent worker/child CPU can exceed elapsed wall time.

Asynchronous cancellation was requested10ms after starting bounded calls and
noticed in Filter after7056 input samples and14 provisional output rows.
Request-to-step-return was0.000360ms, with Cancelled/no final certificate and
no resume after flag clearing. The partially processed fifteenth row was not
published. Lexical scope end took0.000128ms; Session has no heap to reclaim,
so this does not qualify device cleanup/publication quiescence.

All operational gates pass:CPU120s combined plus inherited hard per-process
limit,wall180s,RSS64MiB per process,AS128MiB and1MiB aggregate retained output.
Wall/RSS/output/AS violation probes were refused as registered. Worker and
waited-child HWM readings are at most19904KiB (19.44MiB); waited-child usage includes
the fork/exec process lifetime,not isolated Rust component memory. Actual monitor
records and all inherited limits remain in the raw resource receipts.

The component Session value is41448B,Driver44960B,with40760B array payload and
50476B declared static tables separate. Long-driver PCM/output buffers are
2048/1152B,plus a bounded1159B transport record. Neither whole-source PCM nor
the34.56MB full feature tensor is retained. Exact header/trailer bytes,digests,
record counts,provisional prefix identities and raw stderr are preserved.

Final audit verifies58 artifacts/320752B,294 parent bindings and774 worker runtime
entries (129 per worker,all prebound). All bindings were rechecked after the
complete run; exact source was clean and stable. There are35 validated observations
and120068 emitted rows including the cancelled prefix. Original authority,
reference and asset hashes remain unchanged. Seven integration tests plus one
internal test,HS03/grid/context controls,fmt,Clippy and build checks are recorded
in the implementation receipt. The absent C context oracle remains NOT_RUN.

Raw evidence:
`D:/AI/LIBAPTA/outputs/product-host-frontend-run-ee2a488-20261027/`;
independent audit:
`D:/AI/LIBAPTA/outputs/product-host-frontend-audit-ee2a488-20261027.json`.

This closes only the registered synthetic frontend host experiment. No neural
weights/inference,music labels,training,corpus transfer,physical board,provider
or release ran. VERSION1.0.1 and default/core/wire/consumer/HS01/02/03 are unchanged.
It does not establish the product's5s/60s/50ms targets or whole-pipeline P4 memory.

## Next handoff

Register a separate narrow synthetic linear/LSTM/state/softmax experiment using
small public artificial weights and independent scalar f32 oracles before any
new kernel code. Freeze operation order,reset/no-reset/chunk semantics,state
commit/cancellation,storage/work limits and failure policy. This is a proposed
next registration,not authorization to execute a trained checkpoint or a full
native product port. Model rights/training independence and PF01 measured Guition
JC1060 budgets remain open prerequisites for that larger path; board unavailable,
silicon revision unobserved. Product PF04 remains NOT_RUN,H1 remains deferred.
