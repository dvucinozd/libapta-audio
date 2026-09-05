# Frozen key disagreement topology diagnostic — 2026-09-05

## Procedure recorded before execution

Baseline: `d60e0cd61dacf48d5f251a1b0c403dff443fbeb9`. This continues the
22-case resume boundary using only five retained JSON reports from the spent
72-track development selection. It is a report-only diagnostic, not a native
candidate, corpus evaluation, relabeling or acceptance run. No audio, model,
service or holdout is accessed. The host cost is five small JSON reads; native
CPU, RAM, API/ABI, flags and result bytes are unchanged.

Question: do the OpenKeyScan/Essentia disagreements show a repeated tonal
relationship or native mode asymmetry worth investigating with new evidence?
Neither detector nor their agreement is designated as truth.

Before examining aggregate results, fix the following mutually exclusive,
symmetric pair taxonomy (tonic C=0, major/minor): exact; parallel mode (same
tonic, different mode); relative major/minor (minor tonic nine semitones above
major); same-mode fourth/fifth (interval 5 or 7); other same-mode tonic; other
cross-mode. Also count the directed tonic interval and directed mode pair.
These are descriptive categories, not weighted correctness or rescue rules.

Require exact frozen SHA-256 hashes for default, Essentia, OpenKeyScan,
coordinator and automated-listener comparison reports; unique complete 72-row
coverage; consistent retained keys/labels/agreement flags; exactly 22 external
disagreements; and exact twelve-review/nine-consensus mapping. Stop on any
mismatch. Reject existing outputs. Keep per-track joins private under ignored
`build/`; publish aggregate counts and provenance only.

Report the 22 disagreements and complementary 50 agreements separately, with
the full 72 as context. Count native agreement with each reference, native mode
distribution, pair relationships, and overlap with prior listener reviews.
Listener agreement counts apply only to the intentionally selected reviewed
subset, not the unreviewed remainder or general accuracy. No confidence-safety
pass is inferred from automated agreement; fixes/breaks are not measurable
without an adjudicated baseline/candidate comparison.

Verification: exhaustive 24-key pair taxonomy checks, input/coverage/mapping
rejection tests, frozen-input execution, deterministic replay, and
`git diff --check`. No native rebuild is required. Record tool/test/protocol
hashes and the actual dirty-worktree state; this is not clean-build evidence.
After reporting, stop or specify one separately preregistered representation
experiment. Topology alone cannot establish acoustic causality, select a
winner, or authorize another native detector or automatic relabeling.
