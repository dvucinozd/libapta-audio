# PCM attacks on the selected history lattice

Pre-registered 2026-10-22, baseline
`1876eb34a9169299ece0d908927850e6f6e79670`, feature
`experimental-s6-history` (default off).

Hypothesis: the current local meter's rounded-lag sampling and range cannot
represent an independently selected history window. Capture new temporal
evidence directly from PCM: positive changes in absolute sample amplitude,
accumulated in 256-frame bins. Score accents at the exact Q32 beats of the
selected history grid, then resolve the winning phase through that same grid.
This tests coordinate/evidence coherence, not improved musical accuracy.

Each window is independent. Measure the maximum attack in its beat's own or
adjacent fine bin, clipped to that window. Require 8–128 represented beats;
shorter evidence and larger workloads are explicit unsupported outcomes. Compare
all three triple-meter and four common-meter phase contrasts (mean on-beat
accent minus other accents, normalized by their sum). Require a unique positive
winner; ties and silence are ambiguous. There is no default common-time fallback.
Confidence stays below 50 and state stays provisional. No bar phase is carried
through a boundary, no gap is filled and no earlier local label is relabeled.

Expected cost: optional caller-owned 2048-bin attack ring (two history windows),
one caller-sized record per captured window and at most 128 temporary strengths.
No heap allocation, change to the default estimator or wire representation.
One existing history capture step also validates at most 1024 fine bins and
scores at most 128 beats. Cooperative deadlines are checked between these
bounded steps; this is not a measured P4 runtime bound. Late capture records lost
evidence rather than reconstructing attacks from grid geometry.

Evidence: generated PCM with independently specified accent phase/meter,
silence/gaps, unknown duration, expired-clock/one-step budgets, ring replacement,
capacity loss and retained output/source replacement. No recordings, spent DJ
corpus, formal holdouts or acceptance labels are opened.

Veto: stop for any changed default output, hidden allocation, inferred phase
across windows, coordinate mismatch, missing evidence accepted as silence,
unbounded work, mutation of retained output or automatic stable/final promotion.
Keep the existing actual local-meter rejection. A public experimental result
must expose capture completeness and capacity loss alongside independent copied
grid/meter records. Production consumer gates remain authoritative; provisional
evidence must not be promoted merely to obtain consumer acceptance.

## Implementation and observed verdicts

`WindowHistory::enable_meter` attaches `history_meter::MeterHistory` before
Session attachment. `Session::global_history_meter` exposes verdicts for
the exact grid records inspected by the same bounded capture step. Every
record retains its source window/estimate; old local-meter labels are untouched.

`history_publication::publish_into` preflights the history/meter pair, identity,
lifecycle and all four output capacities before copying provisional segments,
supported ranges, bound meter labels and diagnostic meter records. Returned
views borrow only those independent output banks. Source identity is supplied
and authenticated by the caller, not computed by this library. `upgrade` rejects
stale generations, foreign source/lineage, revision rollback and reinterpretation
under the same revision. Cancelled/failed views cannot be resumed. Source
geometry, including resolved duration, must match for this conservative upgrade;
an unknown-duration live-to-resolved transition still needs an owner policy.
This does not replace the existing product owner or immutable native-result pool.

The generated PCM probe **fails its musical diagnostic** in all three preselected
cases. It remains a diagnostic experiment, with no threshold or estimator tuning
after inspecting these results:

| PCM fixture | Independent period/phase | Existing selected S6 result | New meter verdict |
|---|---|---|---|
| 8 kHz, common time | 4096 frames, accent at beat 1 modulo 4 | 156251 millibpm, anchor at window start; expected tempo about 117188 | 4/4, phase index 0, confidence 44; wrong musical phase |
| 8 kHz, triple time | 4096 frames, accent at beat 2 modulo 3 | 156251 millibpm, anchor at window start; expected tempo about 117188 | 4/4, phase index 0, confidence 46; wrong meter/phase |
| 48 kHz, common time | 16384 frames, accent at beat 1 modulo 4 | 87891 millibpm; expected tempo about 175781 | Ambiguous; no meter label published |

Each case has two complete windows followed by a 17-frame unsupported tail.
The published labels agree exactly with their selected grid's ordinal, whole
frame and Q32 remainder, including the musically wrong grids. Coordinate
consistency is therefore implemented but insufficient for playback acceptance.
The first two cases are known low-confidence errors, not high-confidence PASSes.
The third abstains; none may be counted as an accuracy success.

On a separately declared correct 117188-millibpm lattice, direct generated PCM
attack evidence selects all seven 3/4 and 4/4 accent phases correctly. This
isolates the new evidence/coordinate path; it does not repair S6 tempo-family or
phase selection. The next algorithm experiment must address upstream lattice
selection and independently transfer before any promotion. The existing broad
lag-refinement search is a specific audit target, not a confirmed sole cause.

Unknown-duration, continuously expired-clock/one-step input can starve capture
until EOF. The actual two-window attack ring then loses the first window while
the larger coarse ring retains it. Its meter verdict is `Unavailable`; later
verdicts match prompt capture. No schedule-invariance claim applies after
evidence eviction. Capacity exhaustion, short/too-large beat sets, silence,
discontinuity and ambiguous phases are explicit separate outcomes.

Measured host layouts: meter control 72 bytes, attack bin 16 bytes (32768 bytes
for 2048 bins), meter record 56 bytes, plus at most 512 bytes of strength scratch
and ordinary call temporaries. These are host sizes, not P4 placement/runtime
measurements. The allocation-counter test includes capture and publication.

Evidence root: `D:/AI/LIBAPTA/outputs/rust-s6-lattice-20261022/`; source snapshot,
verification and consumer logs also live under
`/home/daniel/.local/share/libapta-audio/rust-rewrite/lattice-meter-20261022/`.
The final report separates passing software contracts from the rejected musical
probe and unrun corpus/P4 gates. Default C/oracle/CLI evidence is historical;
this opt-in continuation does not rerun or replace that matrix.

## Verification and consumer boundary

Rust 1.95.0 / WSL x86-64 passes 289 default and 315 all-feature workspace tests
in each debug/release profile, 53 focused AddressSanitizer/leak tests, all-target
Clippy with warnings as errors, rustfmt and both no-default library checks.
The allocation-counter path includes the optional attack ring and publication
with zero allocations after caller storage preparation. Seven new tests cover
the new evidence/publication path; the all-feature count includes the explicitly
rejected three-case musical characterization. A passing characterization test
means its recorded failure was reproduced, not that classifier accuracy passed.

The unchanged actual Pajoniiir adapter is inspected at M1 commit
`0f2fb1425ef76f2541a8c76841cfa52178dca589`; its production libapta pin is
`25d09de364e9507532c0508c838a8fecf0772d13`. An external host probe overrides only
that dependency with this exact experimental source. The public native-result
validator rejects the unpromoted diagnostic graph: a completed native result
requires final features and a real Session REVN, neither supplied by this
separate publication. No trusted result or product-owner adoption is claimed.

A separate direct call to the actual subset converter, outside the trusted
adoption path, yields tempo-only/no playback grid and leaves its beat bank
untouched. Its stopped/playing, stale-generation and foreign-identity guards
are exercised. Deliberately forged promotion is rejected transactionally by
its existing full-track/single-meter constraints. This does not establish
consumer acceptance of the experimental grid or exercise the full product owner.
The consumer checkout/pin, production defaults, C estimator, container/wire and
release version remain unchanged.

The subsequent, separately opt-in
[sparse-attack grid experiment](S6-ATTACK-GRID-EXPERIMENT.md) addresses upstream
selection using the new fine PCM evidence. The failed coarse-grid cases above
remain unchanged regression baselines; do not rewrite them as accepted results.
