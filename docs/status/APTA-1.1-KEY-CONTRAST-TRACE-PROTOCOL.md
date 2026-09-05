# Synthetic per-window key contrast trace — frozen 2026-09-05

Baseline HEAD: `d60e0cd61dacf48d5f251a1b0c403dff443fbeb9`, with the preceding
report-only topology work still uncommitted. This is a new diagnostic instrument,
not a replacement detector, corpus run, candidate promotion or acceptance.

Hypothesis: logarithmic compression and cumulative octave-folded evidence
increase a common chroma floor and weaken the major/minor score contrast. The
22-case topology cannot locate this effect; the fixed synthetic progressions
can expose its stage-by-stage mechanics without disputed labels.

## Frozen scope before implementation

- Reuse the exact C generator and 720-row structure of
  `tests/bench/key_mode_diagnostic.c`: 24 keys, clean/detuned/noisy conditions,
  four one-second I-IV-V-I / i-iv-V-i windows, and existing ideal vector checks.
- Add one explicit-build-only `apta_key_contrast_diagnostic` executable. Compile
  its private copy of the native key translation unit with a diagnostic macro;
  the production library does not receive that macro. No public API, session
  state, result-pool, installed symbol or default detector changes.
- At each actual native window-energy calculation observe its sanitized raw
  energy and the actual `logf(1+energy)` value. Collect only one window in fixed
  test-process scratch; no allocation or retained song history. Record raw and
  compressed octave-resolved bins, their folded window vectors, and native
  cumulative chroma. The semitone variant preserves its three-probe averaging.
- Replay the observed compressed additions in original native operation order
  and require exact float identity with accumulated native chroma after every
  window. Require complete per-bin observation and finite nonnegative values.
- Summarize min/mean contrast, normalized entropy, best-major minus best-minor
  raw cosine margin, stimulus-tonic major/minor margin, and diagnostic argmax
  on raw-folded, compressed-window and cumulative vectors. Raw-folded scoring
  is a counterfactual observation, not a candidate or proposed linear frontend.
  Individual IV/V windows are not labelled as global-key errors.
- Keep the existing profiles, thresholds, sample generator and decisions frozen.
  Do not subtract a floor, center scores, fit parameters, or access audio,
  automated services, development labels or formal holdouts.

## Verification and veto

Require default and semitone Release/Werror instruments, sanitizer coverage of
the instrument, exact original diagnostic rows after removing added trace
fields, deterministic replay, and focused summary rejection tests. Compare
uninstrumented key object/analyzer bytes and session layout against the baseline;
any default change, reconstruction failure, incomplete trace, or invalid vector
stops interpretation. Record exact source/tool/binary/report hashes and the dirty
worktree boundary. Full native regression suites are appropriate if default
bytes change unexpectedly; that change itself vetoes this diagnostic scope.

Native runtime/state cost is zero when the macro is absent. Measure the bounded
instrument scratch with `sizeof`; its I/O/runtime are host diagnostic overhead,
not P4 capacity or timing evidence. No safety or accuracy gate is closed.

The output should identify the earliest *observed* contrast change and whether
the synthetic native decisions remain explained by the existing score rule.
Association on these fixtures is not general musical causality. Stop after
recording the evidence and a bounded next hypothesis; a new representation
experiment requires its own protocol and independent development evidence.
