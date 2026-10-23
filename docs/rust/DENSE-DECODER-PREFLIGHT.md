# Dense decoder DD-04: independent replay and operational preflight

Date: 2026-10-23. Previous checkpoint:
`9f9599f31e5d3ad4f99fa2a58b4692071d5052fe`.
Authority: [registered experiment](DENSE-DECODER-EXPERIMENT.md) and
[immutable protocol](DENSE-DECODER-PROTOCOL.json), SHA-256
`5dc1241087a08663b06766f91faade5a5758f9791fd85e0077c62e78d5a186fd`.
The [component-bound preflight](DENSE-DECODER-PREFLIGHT.json) records completed
checks and aggregate generated resource evidence.

**DD-04 PASS: proceed to the single registered DD-05 development attempt after
this checkpoint is committed and pushed.**

## Evidence boundary

DD-04 verifies implementation and operation on generated inputs. Real saved
dense logits, music and reference labels are not opened. No forward inference,
model change, threshold change, musical development verdict, independent
transfer, native port, physical P4 qualification or production promotion occurs.
The registered DD-05 verdict remains **NOT_RUN**.

The production decoder and protocol bytes remain identical to DD-02/DD-01.
The runner adds an independent-verification prerequisite before any reference
label parse; the canonical scorers and mathematical candidate are unchanged.

## Independent verification

[Verifier](../../tools/apta_dense_decoder_verify.py) uses no production
recurrence, tie helper, traceback, quantization or refinement implementation
in its scalar algorithm; its CLI reuses only the runner's binding machinery.
Its scalar tables use phase/interval/row indexing; production uses a flat
row/interval/phase layout. Its own predecessor selection and traceback recover
both meter optima, including the latent initial interval and terminal tie.
An additional arithmetic pass re-sums the recovered objective and checks every
transition, phase, seed and terminal condition without reading DP tables.

Independent f32 validation uses `struct`, quantization uses exact rational
arithmetic, and localization independently evaluates the rational parabola.
Replay checks every output field, abstention, clipping/endpoint metadata,
integer frame, downbeat ordinal and source-window owner. Canonical JSON equality
rejects extra fields and boolean/integer substitutions as well as changed paths.

[Generated checks](../../tests/unit/apta_1_1_dense_decoder_verify.py) cover:

- Exhaustive path enumeration for N=1/10/11/20/24, both meters, null, flat and
  signed competing emissions; a hand-computed three-beat interval-change case.
- Exact 3/4 and 4/4 pulses, changing timing, winner ties, minimum support,
  whole-source gaps, endpoint exclusion, clipping, refinement and window moves.
  Eighteen actual outputs from fourteen existing valid constructor fixture
  methods are replayed, with identical canonical output hashes. Deliberately
  mocked impossible DP paths are wrapper-policy tests, not replay fixtures.
- Independent numeric edge cases, malformed geometry and mutations of either
  meter's score/path, emitted frames, window ordinals, selected meter or schema.
- Fresh verifier import without production/evaluation modules, NumPy or Torch;
  replay still succeeds with production algorithm helpers replaced by failures.

## Complete generated phase machinery

Construction, verification and evaluation run through the actual public CLI
entry functions, outer process-group watchdogs, inner phase processes, pure
source workers, input/runtime binding, candidate freeze and completion reports.
The generated two-source legacy fixture uses the unchanged pinned `mir_eval`
event helper. Labels are physically unavailable during construct and verify.
Evaluation without verification fails before label access; valid evaluation
reproduces the old and new event means exactly and yields synthetic **REJECTED**
against the unchanged registered musical thresholds.

The [test-only bootstrap](../../tests/unit/apta_dense_preflight_bootstrap.py)
injects generated input identities/counts and a generated preflight manifest.
It checks actual Git cleanliness in a disposable generated repository and also
binds the four actual implementation files. It redirects child commands through
the same test harness. Production modules expose no generated-identity override
flag or environment hook. This is generated operational evidence, not a run of
the registered 100-source cohort or proof that its inputs currently reproduce.
The ordinary CLI rejects these generated fixtures.

Negative checks exercise missing/stale verification, existing output, dirty
source, corrupt candidate/freeze, incomplete cohorts and partial worker failure.
Real CLI parent watchdog tests force time, RSS and output-limit failures; DD-03
controls also cover child/grandchild RSS, orphan cleanup, nonzero exits and final
supervisor-report overflow. Failures remain INVALID/INCOMPLETE without musical
verdicts. Label isolation is a software/process boundary, not an OS sandbox.

Five full-size standalone cases (1501 rows, eight windows) cover pulses, flat
positive logits, zero logits, ambiguous meter and changing intervals. Each child
constructs and independently verifies under a 60-second parent deadline,
262144-KiB per-process RSS monitoring, a hard 256-MiB address-space ceiling and
64-MiB output bound. The scalar verifier also has a hard address-space ceiling
in its real CLI worker; its whole-phase watchdog is 1800 seconds.
RSS monitoring includes the supervising process and group descendants, with
20-ms sampling and final self/waited-child peaks; it is not an instantaneous
memory-trace guarantee. Array payload bytes exclude interpreter/temporary costs,
which are represented by measured RSS.

## Completed checks and measured resources

All **226 Python tests** pass: 14 new verifier/operational, 31 runner, 33 decoder
and 148 existing controls, with **zero skipped**. Unchanged Rust controls pass
**37/37 in both debug and release**, together with Cargo fmt, focused Clippy
under `-D warnings`, and the no-default-features library check. The host-only
scope does not rerun the complete C/sanitizer/consumer/P4 matrix.

Full-size generated measurements:

| Case | Emitted beats | Selected meter | Construct seconds | Verify seconds | Child peak RSS KiB |
|---|---:|---:|---:|---:|---:|
| Pulse | 60 | 4 | 1.194 | 3.151 | 31932 |
| Flat positive | 150 | 3 | 1.126 | 3.124 | 31932 |
| Zero | 0 | 3 (abstains) | 1.085 | 2.995 | 31932 |
| Ambiguous | 60 | 3 | 1.141 | 3.114 | 31932 |
| Variable | 55 | 4 | 1.143 | 3.191 | 31932 |

Each case uses **3564000 bytes** of live score/parent payload; the registered
bound is 3566376. The maximum construct-plus-verify compute time is **4.345 s**.
Flat-positive emissions and the ambiguous 3/4 tie are registered objective
behavior, not evidence of correct music detection; no new ambiguity gate is added.

The two-source generated CLI phases take 2.888 s construct, 4.151 s verification
and 7.403 s evaluation. Maximum observed process RSS is respectively 19872,
22168 and 109592 KiB. Combined attempt artifacts occupy **807857 bytes**,
well inside the unchanged 64-MiB cap. Generated musical REJECTED demonstrates
the hard gates; it is not the real-cohort verdict.

Exact commands and full log are retained outside Git at
`D:\AI\LIBAPTA\outputs\dense-decoder-dd04-20261023\verify.sh` and
`verification.log`; generated aggregate reports are `generated-full.json` and
`generated-operational.json`. Their exact SHA-256 identities and implementation
file hashes are recorded in the preflight JSON. Verification log SHA-256:
`ff1ec5b0eadffb27b7a71873d491be80c511676f315790c6237065e60e3eae91`.
These checks ran on the changed implementation bytes based on the previous
checkpoint; component hashes bind them independently of this documentation commit.

## DD-05 handoff

After the completed preflight is committed/pushed, use one clean source revision
for the single registered attempt. First construct all 100 candidates using the
DD-05 command in the experiment. Then run, before evaluation:

```sh
python3 tools/apta_dense_decoder_verify.py \
  --candidates /home/daniel/.local/share/libapta-audio/rust-rewrite/dense-decoder-v1/candidates-attempt-1 \
  --legacy /home/daniel/.local/share/libapta-audio/rust-rewrite/neural-reference-20261022/comparison-source-domain
```

Verification creates new sibling directories `CANDIDATES-verification` and
`CANDIDATES-verification-supervision`. Evaluation automatically requires both
reports to pass and bind the exact candidate freeze, verifier bytes, source
revision, source count, protocol and input/runtime identities before labels.
Construction, verification and evaluation artifacts share the registered
64-MiB attempt cap. Run the registered evaluation command only after verification.

Preserve all partial evidence. No automatic real-data retry, tuning, fallback,
transfer or port follows a failure. A valid musical failure is REJECTED; passing
all development gates is DEVELOPMENT_PASS_ONLY. Product RAM/latency budgets,
independence, confidence calibration and physical P4 evidence remain open.
