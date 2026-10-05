# Native S6 coverage and DSP boundaries

The native correction below supersedes the original same-tempo interior bridging
and segment-overflow behavior. Earlier measurements remain historical C/Rust
compatibility evidence; C itself is unchanged.

## Rejected-window consolidation correction — 2026-10-05

[GlobalAnalysis](../../rust/src/global_analysis.rs) now consolidates accepted S6
windows only when their frame ranges are adjacent **and** nominal tempos differ
by at most the existing 1500 millibpm threshold. A rejected window therefore
remains outside every segment. Estimation, rejection thresholds, phase selection,
PCM accounting, cooperative scan scheduling and EOF follow-up are unchanged.
This improves **coverage honesty**, not musical accuracy or missing-timing recovery.

The smallest adjacency-only patch was insufficient: eight-segment overflow used
to extend the last segment through later incompatible windows. Rust now omits
unrepresentable windows and sets the existing degraded flag, retaining the exact
represented prefix. An adjacent compatible window can still merge at capacity;
a later window cannot merge through an omitted window. No extra storage, public
format, dependency or history-retention framework is introduced.

Multiple segments alone no longer imply dynamic tempo. The represented tempo range
must span more than 1500 millibpm to set that flag. Equal/similar-tempo islands
remain `Segments` unless dynamic output was explicitly requested. Explicit dynamic
requests still produce `Hybrid`, using only beats inside supported islands; the
existing explicit-beat ceiling stays in force. This does not resolve hybrid
transport authority or qualify exhausted beat arrays for full-source use.

### Concrete before and after

The existing 8 kHz interior fixture has silence throughout [262144,524288).
Unchanged C (and pre-correction Rust) reports one [0,786432) segment. Corrected
Rust reports [0,262144) and [524288,786432). Both retain nominal tempo 96154
millibpm. The new suffix anchor is 526336; the prefix anchor remains 2048.
Each island has 53 beats. Segment representation retains independent anchor
ordinals of zero; requested hybrid output enumerates 106 supported beats with
suffix ordinal 53. These ordinals enumerate represented beats, not inferred
beats in the gap. No beat or transport timing is fabricated for the silence.

Revision remains 6/5 in the five established interior profiles because their
number of geometry changes is unchanged; revision IDs are local sequence numbers,
not cross-implementation content hashes. The internal signature covers segment
geometry and now grid flags, so capacity degradation of otherwise unchanged
geometry advances revision and mutation serial. Repeating the same result does
not advance them. Retained prior graphs never change retrospectively.

The established 4096017-frame changing-window capacity fixture previously extended
segment eight [2621440,4096017), with 1153 hybrid beats and revision 47/46. Rust
retains [2621440,2883584), 758 beats and revision 34/33. Its first seven segments,
anchors, periods and confidences are unchanged; all output segments/explicit
beats carry the corrected revision. Both have flags 130 (dynamic plus degraded).
The smaller revision sequence follows omitted geometry updates, not normalization.

### Range, publication and consumer contracts

The existing single evidence/applicability/coverage envelope remains unchanged.
In this implementation it is derived from inspected input, **not the union of
accepted timing support**. This is a retained limitation, not a new definition of
coverage: [normative beatgrid coverage](../../specification/beatgrid.md#13-coverage-gaps-and-inference)
describes disjoint supported ranges. Do not claim complete native coverage-range
conformance from this fix. The [v1 segment contract](../../specification/global-grid-container.md#33-segment-record)
permits ordered nonoverlapping segments, so the corrected segment claims expose
the discontinuity without new records or fields. Final still denotes completed
analysis, not full-source timing. Consumers must check segment geometry and
selected authority, not just Final or the enclosing range. The unchanged tail
and resident-prefix limitations below still apply. Fully expressing every island
in coverage metadata needs a separate compatibility design for the single-range
wire subset; this task does not silently broaden that format.

The existing immutable publication path copies these segments, signatures drive
normal change detection, and both slots are already provisioned for eight segments.
A new publication regression holds a prefix lease through exhaustion, checks its
bytes, retries after release, drains EOF one step at a time and retains the final
gapped graph after writer destruction. The 18-profile test also retains actual
caller-owned prefix graphs independently through processing, EOF and destruction.

An external probe links the corrected library to the **unchanged** isolated
Pajoniiir adapter via a probe-only Cargo patch. Actual known/unknown PCM sessions
using 256-frame/one-step work reject with `UnsupportedGrid` without writing the
destination or falling back to local timing; a local-only control is accepted.
A separately labelled validation fixture binds meter to a real global beat and
uses consecutive represented ordinals to isolate the gap check; it still rejects.
Changing only the second range start to a deliberately false joined range makes
that artificial control pass, demonstrating why producer segment honesty matters.
These controls are never delivered or treated as musical corrections. No consumer
source, pin, production provider or PR scope changes.

### Regression evidence and compatibility impact

- [s6_ring.rs](../../rust/tests/s6_ring.rs): the same 18 cases retain exact complete
  expected GGRD/REVN payloads for both original C and corrected Rust in all five
  same-tempo interior cases. Eleven unaffected cases retain six-payload exact
  parity. Two bounded ring-replacement cases retain the separately characterized
  native EOF difference. WOVR/TEMP/LGRD/MTRD match C in all 18 cases.
- [tempo_analysis.rs](../../rust/tests/tempo_analysis.rs): the existing capacity
  fixture asserts both complete expected C/Rust grid and revision payloads,
  including every generated Q32 beat. All other comparisons remain exact.
- Private-stage tests cover adjacency, the 1500/1501 threshold, equal-tempo gaps, three-island cumulative tempo variation,
  eight/ninth-segment boundaries, post-omission nonmerging, compatible merging at
  capacity, flag-only revision changes and requested/unrequested hybrid behavior
  at the 3072-beat ceiling. These controlled windows test consolidation contracts,
  not estimator accuracy; PCM-driven tests provide the integration evidence.
- [musical_publication.rs](../../rust/tests/musical_publication.rs) adds the
  actual publication/exhaustion/retention regression described above. Existing
  lifecycle, clocks, allocation and unaffected DSP tests remain required.

New raw evidence, exact source/configuration hashes, serial commands, original
and corrected outputs, consumer probe and final acceptance/publication are under
`/home/shome/.local/share/libapta-audio/rust-rewrite/s6-adjacency-20261005/`.
The original `s6-interior-ring-20261005/` evidence is preserved. Run the combined
suite as below with `APTA_S6_EVIDENCE_DIR` unset. For fresh focused artifacts,
set it to a new external directory for each s6_ring invocation or the selected
`native_global_grid_and_revisions_match_c_windows` tempo_analysis test.

C remains unchanged as historical compatibility evidence. Rust deliberately
changes these defective grid/revision payloads while retaining API/container
compatibility. Musical accuracy, exact local-meter/global-grid binding, missing
S6 tails and accumulated full-source history are **not** repaired. No recordings,
labels, unopened holdouts, numerical backend or frozen thresholds were changed.
A coherent streaming history needs retained accepted evidence and defined revision
boundaries; splicing earlier prefix and current suffix outputs is not sufficient.
Slow/clock-limited scans after eviction still need independent frozen-evidence
validation. Production adoption and physical hardware gates remain separate.

## Original evaluation — 2026-10-05

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

The follow-up evaluation below audits interior rejected windows and S6 ring
replacement using the same oracle, with its bounded EOF discrepancy explicit.
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

## Interior rejection and actual S6 ring replacement — 2026-10-05

**Historical pre-correction results:** the correction above supersedes the native
same-tempo row and its five parity claims. Original C outputs remain unchanged.

[s6_ring.rs](../../rust/tests/s6_ring.rs) adds a separate **18-case** evaluation
using the same unchanged public C oracle. It exercises the actual S6 ring of
16384 × 2048 = **33554432 frames**, not the smaller S4 ring. No production Rust,
C, numerical backend, consumer pin or acceptance policy changes.

The four repository-generated, 8 kHz mono fixtures retain the 64-sample linear
impulses at amplitude .75. Interior cases contain 384 bins: pulse period 4000
frames, silence throughout bins [128,256), then either the same period or period
6000. Ring cases contain exactly 33554432 frames and 33556481 frames (one extra
whole bin plus one sample), with period 4000 throughout. Each runs known and
initially unknown duration with unlimited work and a finite 32-step budget.
Interior cases additionally request dynamic output with known duration and
unlimited work. Masks are 1081 and 1145 respectively. The 4096-frame submissions
match C; no soft deadline or injected clock is used. This is not one-step or
all-budget parity. Existing clock/publication tests remain independently required.

### Measured coverage

| Fixture | Native evidence and segment extent | Interior gap | Revision |
|---|---|---:|---:|
| Interior, same tempo | [0,786432), one segment | 0 | 6, previous 5 |
| Interior, changed tempo | [0,786432), two segments | 262144 frames | 6, previous 5 |
| Exactly full S6 ring | [0,33554432), one segment | 0 | 384, previous 383 |
| Replaced ring with partial EOF | [4096,33556481), one segment | 0 | 385, previous 384 |

The wholly silent interior window has zero energy/flux and is rejected. Both
implementations consolidate the next accepted window into the previous segment
when nominal tempos differ by at most 1500 millibpm, **without an adjacency
check**. Thus same-tempo consolidation spans rejected evidence. A larger tempo
change instead leaves [262144,524288) uncovered between the two segments. Both
results declare full evidence coverage and Final state; neither flag nor revision
identity identifies the rejected window. The changed-tempo result carries the
dynamic flag and hybrid representation, not a gap-specific degraded flag. These
are inherited algorithm/coverage limitations. Segment continuity is necessary
for full-source transport but cannot establish that every interior window was
accepted or that the timing is musically correct.

At the ring boundary, accepting the extra whole bin and partial bin replaces
resident identities 0 and 1. EOF completes that final partial bin, so the current
resident evidence starts at bin 2/frame 4096. Latest output does not retain the
old prefix timing as accumulated full-source history. Requested range remains
[0,33556481), while evidence/applicability/coverage and segment range start at
4096. Completed-session views still report Final. The prefix loss and lack of
full-source accumulation are inherited streaming limitations; Final does not
repair them. All 18 native cases reject exact local-meter/global-grid binding.

### Explicit bounded EOF discrepancy

Six payloads (WOVR, TEMP, LGRD, GGRD, REVN, MTRD), including absence, compare
exactly for **16 cases**. In the two replaced-ring cases with 32-step processing,
WOVR/TEMP/LGRD/MTRD still match, but **GGRD and REVN do not**:

- C retains [0,33554432) and revision 384/383, losing the final 2049 frames.
  Its requested range nevertheless ends at 33556481, and its view is Final.
- Rust refreshes [4096,33556481) and revision 385/384, exactly matching its
  unlimited-work GGRD/REVN. It loses the replaced prefix rather than the new tail.

The test explicitly asserts this difference. C's entire REVN equals the full-ring
reference; every GGRD byte equals that reference except the explicitly checked
requested EOF field. Native GGRD/REVN equal the unlimited replaced-ring output.
No bytes are rewritten, revision identities normalized, tolerance introduced or
failed comparison silently skipped. This is **not blanket Rust/C parity**.

The read-only external C trace shows all 33556481 frames accepted, zero queued
PCM, EOF signalled, no deadline, and the frozen [0,16384)-bin scan still active.
It commits during drain. In
[C refresh](../../src/beatgrid/apta_s6.c), commit sets
`refreshed_after_end_of_input=1` and requests follow-up for changed EOF evidence;
the next entry's
[refresh gate](../../src/beatgrid/apta_s6_internal.h) skips it because only two
new bins arrived, below the 32-bin threshold, and clears pending. The session
completes with the old geometry. The diagnostic links unchanged C and reproduces
the original oracle containers byte-for-byte for known/unknown and both budgets.

[Native refresh](../../rust/src/global_analysis.rs) instead keeps
`refreshed_eof=false` while follow-up is required. This behavior predates this
work (introduced with the musical lifecycle continuation). The newly measured
payload consequence is an explicit **native EOF lifecycle difference**, not a
new Rust algorithm fix or numerical discrepancy. Reverting it merely to reproduce
C's stale EOF result is not justified. Correcting C's gate would be a separate
C lifecycle change with its own compatibility review; C remains unchanged here.

### Retention, reproduction and remaining work

Every profile copies an actual prefix `SessionSnapshot` at 262144 accepted
frames into independent caller-owned `OwnedResult` arrays, using generation 17.
After all remaining input, EOF, ring replacement and writer destruction, its
serialized bytes, original source-duration identity and generation remain
unchanged. Earlier prefix timing survives in that retained result; the latest
result does not combine it with the suffix. This checks graph ownership, not
C/native intermediate generation scheduling or a new prefix-merging policy.

Run the existing combined runner with Rust 1.95.0 and two build/test jobs. It
includes the ignored external-C group in both profiles. For raw evidence, set
`APTA_C_TEMPO_ANALYSIS_ORACLE` as above and set `APTA_S6_EVIDENCE_DIR` to a **new**
external directory, then run:

```sh
cargo test -p libapta --test s6_ring -- --ignored --nocapture
# Repeat with --release and a different new evidence directory.
# Unset APTA_S6_EVIDENCE_DIR before running the combined suite.
```

Dated evidence:
`/home/shome/.local/share/libapta-audio/rust-rewrite/s6-interior-ring-20261005/`.
Debug/release directories contain four PCM inputs, paired final containers,
retained prefix containers and CSV measurements including the differing C ranges
and revisions. External `trace.py`/`trace-oracle.c` retain the unchanged-library
EOF diagnostic. The manifest and complete dated handoff own hashes, commands,
verification, publication and CI. Fixture/graph/serialization allocations are
outside the separately measured core allocation tests.

Recommendation: preserve explicit rejection of incomplete or unsupported timing.
Do not concatenate old prefix and new suffix revisions or extend a segment to
claim new timing. A future full-source streaming design must define evidence
retention, revision boundaries and accepted-window coverage together. The correction above now rejects consolidation across unsupported windows.
Meter coupling and musical accuracy still require separate evaluation. This synthetic evaluation
consumes no recordings or holdouts and changes no frozen thresholds. Sparse gaps,
multiple complete ring turnovers, slow one-step/clock-budget scans across
replacement, long hybrid beat exhaustion, ABI/platform and physical P4 acceptance
remain separate work.
