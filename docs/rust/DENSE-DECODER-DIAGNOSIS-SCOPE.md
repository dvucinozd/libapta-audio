# Frozen DD-05 failure diagnosis: scope before private inspection

Date: 2026-10-23. User-authorized continuation of the rejected DD-05 attempt.
This is a post-hoc audit of already-observed development evidence, not a new
candidate registration, retry, acceptance or amendment of its rejection.

Authority: [DD-05 verdict](DENSE-DECODER-DD05-20261023.md), immutable result JSON
SHA-256 `40b3ac60c3e19d7bdb747be78c5f345d7ec96f2c7c8938c2f55dc6925ce37b59`.
Original measured implementation: `8e27cf1495cc5d496a3c251b0e5c825c56dc81d6`.

Use the saved 100 candidates, private per-source score files, phase completion
reports/parent envelopes and the exact previously observed reference manifest.
Bind their original hashes and original ordered 100-source identity. Do not
decode raw logits again, change emitted events, read audio, perform new inference,
rescore an alternative candidate, tune parameters or open another corpus.
Original measurement-source bindings refer to that historical checkout; this
audit separately records its clean source revision rather than pretending the
later documentation commit is the original measurement revision.

The [read-only audit tool](../../tools/apta_1_1_dense_decoder_diagnosis.py) will:

1. Reproduce every saved actual `score_points` result with the canonical scorer,
   and reconcile complete counts, support, unknowns and all 95 actual joint breaks.
2. Explain each failed window by the original conditions: incomplete reference,
   empty, outside-reference points, unknown successor, phase error, known-period
   error. Retain combinations and the nominal-correct/actual-failed subset.
3. Decompose each original known interval exactly as
   `actual_delta = 960 * delta_row + delta_saved_refinement`. Keep its original
   emitted position and original nearest reference/local period fixed. Tally the
   2x2 arithmetic tolerance outcomes of the row term and actual interval. This
   does not create or score a row-centre candidate or choose a fallback.
4. Record fixed absolute relative-error bins <=1%, (1,2]%, (2,5]%, >5%, plus
   nearest-rank p50/p90/p99/max; absolute saved offsets and their adjacent
   differences; integer row-interval changes and successor availability.
5. Count local reference targets for which no interval d10..75 on the 960-frame
   lattice can reach 1%. This is local arithmetic coverage, not a reachable
   detector path, candidate success bound or parameter search.
6. Audit adjacent in-source reference gaps against the canonical five-nearest-gap
   median target at the original reference point. This is annotation/metric
   self-consistency, not a scored perfect-reference candidate or relaxed gate.

All source IDs, coordinates, label values and per-source details remain outside
Git. Publish aggregate counts/distributions, hashes and actual audit scope only.
Validate classification and numeric decomposition on generated tests first;
commit/push the audit implementation/scope before the saved-data audit.
Resource supervision uses Linux parent watchdog, 1800 seconds, 262144 KiB per
process and 64 MiB for new audit artifacts; this does not redefine DD-05 limits.

Afterward, preserve the original REJECTED result, distinguish observations from
causal hypotheses and document a next decision. No new model, training, transfer,
native port or new real candidate run is authorized by this audit scope.
