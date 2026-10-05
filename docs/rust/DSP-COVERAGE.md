# Native S6 coverage and DSP boundaries

## Evaluation — 2026-10-05

The native port reproduces the C reference's incomplete S6 segment coverage.
`Final`, complete PCM processing and a full declared grid coverage range do
**not** establish that the segments provide timing throughout that range.
Extending a segment to EOF would change the original algorithm's output and
would not resolve the separate local-meter/global-grid disagreement.

No production algorithm, numerical backend, acceptance threshold, C source,
portable API or consumer policy changed in this evaluation.

### Reproduction and scope

[s6_coverage.rs](../../rust/tests/s6_coverage.rs) uses the existing
[public C oracle](../../rust/tests/fixtures/tempo_analysis_oracle.c) and the
original repository-owned 8 kHz mono 120 BPM impulse fixture: every 4000 frames,
64 samples fall linearly from amplitude 0.75. These are synthetic invariants,
not recordings or evidence of general musical accuracy. “Real S6” in the earlier
consumer handoffs means actual PCM-driven S6 processing, not real music.

Ten inputs cross the 64-bin minimum, 128-bin window, short EOF-bin boundary and
a full silent final window. Each runs with known duration/segments, initially
unknown duration/segments, and known duration/dynamic output: **30 cases**.
The C masks are WOVR|BPM|LGRD|GGRD|MTRD (1081), with DYNAMIC (1145) in the third
profile. Rust explicitly attaches the corresponding stages. Every present or
absent WOVR, TEMP, LGRD, GGRD, REVN and MTRD payload compares byte-for-byte in
debug and release. No tolerance, revision normalization or rewritten oracle is
used. Whole-container feature masks are not compared by this test: native
content-derived availability and C requested-capability projection are distinct
interfaces, already covered by the existing publication tests.

All source frames are accepted and processed, EOF resolves the unknown source
length, and sessions drain to Complete before comparison. The test uses
4096-frame submissions and unlimited per-call work. The separate unchanged
consumer test `real_global_pcm_cache_and_consumers` reproduces transactional
rejection using 256-frame/one-step processing and unchanged retained output.

| Source frames | Last S6 segment end | Observation |
|---:|---:|---|
| 129024 (63 bins) | absent | Below minimum evidence |
| 131072 (64 bins) | 131072 | Minimum accepted window |
| 262144 (128 bins) | 262144 | One whole window |
| 262145 | 262144 | One-frame tail not represented |
| 320000 | 262144 | Original consumer rejection: 57856 missing frames |
| 391168 (191 bins) | 262144 | 63-bin final window rejected |
| 391169 | 391169 | Partial EOF bin supplies the 64th bin of the tail |
| 393216 (192 bins) | 393216 | 64-bin final window accepted |
| 524325 | 524288 | Short tail after two whole windows rejected |
| 524288, silence after 262144 | 262144 | Even a full tail window can lack usable flux |

Every non-absent grid in this matrix is Final and declares coverage ending at
the source length, including rows whose segment ends early. All three profiles
have the same coverage outcomes. The 320000-frame fixture loses 7.232 seconds
of segment timing (18.08% of the source), despite complete input coverage.

### Mechanism and classification

The paired implementations are
[Rust GlobalAnalysis](../../rust/src/global_analysis.rs) (`window`, `refresh`,
`commit`) and [C S6](../../src/beatgrid/apta_s6.c)
(`apta_s6_estimate_window`, `apta_internal_s6_refresh`). Both:

1. collect 2048-frame bins and process consecutive windows of at most 128 bins;
2. reject a window shorter than 64 bins, or one with insufficient correlation;
3. add segments only for accepted windows; S4 fallback occurs only when **no**
   window succeeded, not for a missing tail after a successful window;
4. clamp the last segment to EOF without extrapolating it;
5. derive declared coverage and Final state from contiguous input evidence,
   independently of accepted segment coverage.

This is an **inherited coverage limitation**, not missing input, an EOF omission,
a Rust/C compatibility defect or a consumer-capacity rejection. It also exposes
a distinction between declared evidence coverage and actual timing coverage.
It must remain visible to consumers requiring full-source timing.

The same non-absent cases select S6 tempo **96154 millibpm**, while S4 selects
**120001 millibpm**. The generated input period is exactly 4000 samples, or
120000 millibpm. S6's error on this synthetic invariant is about 19.87%; that is
an **original estimator limitation shared with C**, not floating-point noise.
It is not an estimate of musical corpus accuracy. Every case also fails exact
local-meter binding to S6. For the 320000-frame unlimited-work profile, meter
downbeat ordinal 3 is at frame 13279; the global segment starts at frame 2048
with its own Q32 period. Even the full-length rows fail the binding check.
Appending the missing tail cannot make those different timing models agree.

### Streaming, clocks, masks and retained results

[Session](../../rust/src/session.rs) sends accepted PCM to S4/S6, processed PCM
to key, resolves total duration at EOF and drains pending musical work before
completion. Waveform/S4/S6/meter/key publication order, independent mutations,
request projection and retained generations are established by
[musical_publication.rs](../../rust/tests/musical_publication.rs), including
sampled clock callbacks and exhausted-slot retries. Completing a session does
not retrospectively change retained earlier generations or prove full timing
coverage. Requesting dynamic output changes representation, not missing evidence.

A separate bounded probe of the existing C oracle at 131072 frames, initially
unknown duration, mask 1081 and `maximum_steps=1` exhausts its 100000-call drain
guard. This evaluation does not claim C termination parity for that profile.
The native port already documents deliberate termination corrections for final
partial scans and repeated rejected ensemble proposals in
[MIGRATION.md](MIGRATION.md#integrated-musical-analysis-continuation--2026-10-04).
No extra drain limit, fallback or scheduler change was introduced to hide the
probe failure. The coverage matrix isolates final payload behavior with unlimited
per-call work; the original consumer separately establishes its native bounded
execution and rejection. Sparse gaps, S6 ring replacement, arbitrary sample-rate
accuracy and every budget/mask combination are outside this new matrix.

### Portable key boundary is separate

The existing `trace_portable_backend_boundary_against_compiled_c` audit still
reports portable selected score 55734 versus host C 55735, with the same key
selection and remaining candidate scores. Coefficient 27 first differs at
argument bits `3ffa3924`: portable `bf3fd897`, host C `bf3fd898`. Host cosine
and logarithm reproduce the sampled C intermediates. The existing
[90-digit Decimal diagnostic](../../rust/tests/fixtures/key_rounding.py) finds
the portable coefficient nearer the high-precision value. This is a documented
**math-backend quantization boundary**, not justification for a special-case
coefficient, relaxed comparison or portable backend switch. Debug and release
audits and the Decimal calculation were rerun; no key algorithm was changed.

### Independent accuracy and recommendation

The authoritative historical musical failure is the independently reviewed
[60-track DJ report](../status/APTA-1.1-FINAL-DJ-CORPUS-STATUS.md), with
[tracked provenance](../../evidence/1.1/dj-acceptance-provenance.json) and
[aggregate raw report](../../evidence/1.1/dj-acceptance-report.json). That is a
dated C algorithm evaluation, not a new Rust musical benchmark. Its key,
downbeat, beatgrid and safety failures remain open. No new recording was scored,
no label was invented, and no unopened holdout was consumed here.

Keep full-source segment coverage, exact meter binding and supported authority
checks mandatory. Do not infer them from Final or declared coverage, choose
local timing after a rejected global result, or accept hybrid arrays as an
unambiguous override policy.

The next independently useful evaluation is a source-linked audit of interior
rejected windows and S6 ring replacement, using the same exact oracle comparison.
An algorithm change needs a separate approved candidate and evaluation protocol:
overlapping/rebalanced final windows would re-estimate timing and can change
existing phase/tempo/revision values; extending an existing segment would instead
assert timing without new evidence. Neither fixes meter coupling or original
musical accuracy automatically. A policy that labels/exports partial grids must
also preserve explicit coverage and cannot authorize full-track transport.

### Commands and raw evidence

Run `rust/check.py` as documented in [README](../../rust/README.md) first to
build the unchanged oracle. From the repository root:

```sh
export RUSTUP_TOOLCHAIN=1.95.0 CARGO_BUILD_JOBS=2 RUST_TEST_THREADS=2
export CARGO_TARGET_DIR=/home/shome/.local/share/libapta-audio/rust-rewrite/combined/cargo-target
export APTA_C_TEMPO_ANALYSIS_ORACLE=/home/shome/.local/share/libapta-audio/rust-rewrite/combined/tempo-analysis-oracle
# Optional: a NEW external directory, created exclusively by the test.
export APTA_S6_EVIDENCE_DIR=/absolute/external/new-s6-run
cargo test -p libapta --test s6_coverage -- --ignored --nocapture
# Use a different new evidence directory for --release; unset for combined checks.
unset APTA_S6_EVIDENCE_DIR
export APTA_C_KEY_MATH_ORACLE=/home/shome/.local/share/libapta-audio/rust-rewrite/combined/key-math-oracle
cargo test -p libapta --lib trace_portable -- --ignored --nocapture
cargo test -p libapta --release --lib trace_portable -- --ignored --nocapture
python3 rust/tests/fixtures/key_rounding.py
```

The dated external root is
`/home/shome/.local/share/libapta-audio/rust-rewrite/dsp-coverage-20261005/`.
`debug/` and `release/` retain generated PCM, both complete C/Rust containers for
every case, and `coverage.csv`. Logs retain the key audit, consumer rejection,
bounded C drain failure, initial development failures and full combined checks.
The external handoff and manifest own exact source/tool hashes, publication and
CI state. Allocation measurements remain in the existing isolated allocation
tests; allocating test fixtures and artifact serialization are not allocation
or embedded-memory qualification.
