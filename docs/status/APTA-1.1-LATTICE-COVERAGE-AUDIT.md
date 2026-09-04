# APTA 1.1 lattice candidate-coverage audit

- Status: diagnostic protocol fixed before its first corpus execution
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
