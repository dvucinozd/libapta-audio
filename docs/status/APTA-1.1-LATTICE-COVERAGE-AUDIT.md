# APTA 1.1 lattice candidate-coverage audit

- Status: diagnostic completed 2026-09-04; no selector change justified
- Baseline: `e0369e2ce4f0bf44c0b9c14c9ef32f97012d2785`
- Evidence: existing ASAP/Ballroom development traces only
- Acceptance claim: false; no candidate promotion or holdout authorization

## Question

S4 retains three individual lag bins before refined-tempo duplicate removal.
WP2 selectors can only choose those three. Quantify whether missing true periods
come from the scan range, weak correlation, the tempo prior, or neighboring bins
occupying the finite candidate slots. Do not assume that peak diversity solves
the problem. In particular, a label-informed oracle is not a usable selector.

## Fixed diagnostic

Reuse the frozen onset oracle's 40-300 BPM range, normalized autocorrelation,
125 BPM prior, lower-lag tie order and 1% period tolerance. Independently expand
the same calculation to all scanned lags and require exact top-three lag
agreement with both the frozen oracle and captured trace before reporting.

For each development trace report:

- scan-range and integer-lag representability of the annotated median period;
- first correct lag's rank with and without the unchanged prior;
- correct-period coverage of the original three lag bins;
- diagnostic coverage of three distinct local score maxima, where a plateau
  contributes its lowest lag and endpoints compare only with their one neighbor;
- annotation interval agreement with the median within 1% over the evidence
  window, to distinguish a constant-period proxy from variable-tempo music.

Local maxima use the existing scores unchanged, no suppression radius, fitted
weight, threshold, or label-dependent routing. They are a counterfactual
coverage audit only: no phase, confidence, native analyzer or result changes.
The entire positive ranked scan is diagnostic, not a widened public top-K.

## Safety and resources

Reject duplicate/private IDs, unexpected split topology, non-finite/negative
flux, invalid metadata, candidate mismatch, labels-hash or trace-set-hash drift.
Pin inputs to the exact WP2-I3 hashes recorded in its protocol. Require exactly
40 development and 40 disjoint holdout IDs, with 20 tracks of each supported
meter in each split. Open trace files for development IDs only. The label file
is parsed for topology; holdout beat annotations are never scored.

This is a host-only NumPy tool. Native RAM, CPU, 16-byte onset bins, API/ABI,
wire format, Q32 and defaults are unchanged. Synthetic tests precede the first
corpus execution; commit this protocol and implementation before that run.
Report exact inputs, tool revision, deterministic report hashes and limitations.
Any subsequent detector requires its own preregistration; none of WP1-I1-I10 or
WP2-I1-I3 is reopened by this audit.

## Frozen development outcome — 2026-09-04

The protocol, tool and initial 11 synthetic tests were committed at
`61248338f759438a1f65e739b0eb28411634e20d` before the first corpus run. Both
40-track development sets passed pinned-input and exact captured-candidate
agreement checks. Later CLI hardening enforces exact clean HEAD and refuses
report overwrite; it does not change any score or metric.
The complete synthetic suite now passes 13/13, including rejection before
input access for dirty/mismatched revisions and existing output files. Run it
with `python tests/unit/apta_1_1_lattice_coverage_audit.py` in an environment
with NumPy. Windows-owned linked worktrees evaluated from WSL must use
`--git-executable '/mnt/c/Program Files/Git/cmd/git.exe'` for the ownership check;
plain Linux Git cannot resolve a Windows-format linked-worktree pointer.

| Diagnostic count | ASAP / 40 | Ballroom / 40 |
|---|---:|---:|
| Annotated median period representable in scan | 38 | 40 |
| Original top-three period coverage | 2 | 17 |
| Three local maxima period coverage | 2 | 12 |
| Local maxima fixes / breaks | 0 / 0 | 0 / 5 |
| Tracks with neighboring top-three lag bins | 0 | 14 |
| Correct period within first 10 weighted lags | 5 | 18 |
| Correct period within first 10 raw-correlation lags | 3 | 18 |
| Tracks with <50% annotated intervals near median | 39 | 23 |

Distinct local peaks do not recover a missing correct period on either family
and lose five on Ballroom. Removing the prior also does not improve top-ten
coverage. Therefore neither neighboring-bin de-duplication nor prior removal
is justified as the next native change. This is not a test of a new onset
representation or a time-varying beat tracker, and does not reject those
unimplemented hypotheses.

The interval statistic describes the limitations of a constant-median-period
proxy, especially for variable-tempo piano. It does not establish incorrect
annotations and does not change the frozen native/refined-Q32 grid evaluator.
Cached top-three oracle coverage is not published LGRD accuracy and must not
be substituted for WP2-I3's selected-period or joint-phase scores.

Local reports (ignored; opaque development rows only):

- `build/takeover-lattice/asap.json`, SHA-256
  `d1a2f5daa92f3e18d2c9d3d0372136e4d9f26bf4a6fa23754cd3c054b30ecdc7`;
- `build/takeover-lattice/ballroom.json`, SHA-256
  `a627be298b24c7200c67e7467946bb0ac8b57eb268a23f917ac5ab3c40ea7365`.

Input pins remain the WP2-I3 pins embedded in the tool. No native code, default
bytes, runtime state, acceptance threshold or formal holdout changed. Next
native work requires a separately frozen new-evidence or time-varying-lattice
hypothesis and an oracle gate before integration; additional rescoring of
these same exhausted lag scores is not a promoted solution.
