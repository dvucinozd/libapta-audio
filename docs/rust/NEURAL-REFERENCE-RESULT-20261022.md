# Fixed neural reference comparison — measured result

Date: 2026-10-22. Exact clean run source:
`f00d482f775c51621ca03136d6e4941232cf24f4`, committed and pushed before the run.
The [registered experiment](NEURAL-REFERENCE-EXPERIMENT.md), immutable parent
protocol, metadata-availability amendment and documented exclusive-endpoint
adapter repair bind this attempt. The
[machine-readable aggregate](NEURAL-REFERENCE-RESULT-20261022.json) owns the
complete numbers, per-genre means, resources and evidence hashes.

## Decision

**Keep Beat This! final0 as the measured offline reference; reject a port of
the original BeatNet model 1 under this protocol.** BeatNet fails all four
registered event/continuity screening gates. Beat This! passes those four
gates on this fixed benchmark cohort, but fails the no-regression promotion
veto after constant-grid projection. Neither model is a qualified P4 detector.

The pre-measurement choice of BeatNet as the first P4 candidate was based on
its small causal architecture. These measurements supersede that provisional
choice: small weights do not compensate for inadequate musical evidence.
No model, seed, threshold or corpus selection was changed after scoring.

The next step selected by this comparison was a bounded
**event-to-APTA representation audit** using the saved
Beat This! event sequences, examining existing explicit/segmented grid support,
same-grid meter/downbeat alignment, gaps and consumer capacity limits. Separate
remaining neural event errors from constant-period projection loss. Preserve
the unchanged scorer and failed regression cases. Do not write a new backend
or train another model before this resolves what the output must represent.
Any later compact/causal model requires a separately registered comparison;
BeatNet+ licensing remains unresolved.

That [representation audit is now complete](EVENT-REPRESENTATION-RESULT-20261022.md).
Exact raw events retain only 10/401 joint-correct windows versus OLS 187/401;
both original regressions remain, and exact segment compression exceeds the
wire cap on 98/100 sources. The subsequent separately registered
[multi-beat reconstruction](MULTIBEAT-TIMING-RESULT-20261022.md) is also rejected:
188 versus 187 joint-correct windows, but twelve OLS joint breaks. The subsequent
[PCM-anchor refinement](PCM-ANCHOR-TIMING-RESULT-20261022.md) is also rejected:
186 versus 187 joint-correct windows, with three OLS joint breaks. The current
next proposal is read-only failure attribution before another estimator choice.
The measured model comparison and its frozen scores remain unchanged.

## Event results

Both fixed checkpoints complete all 100 GTZAN sources, ten per genre, from the
same canonical PCM and resampling path. Scores are macro means over all sources,
with a five-second trim and 70 ms event F1 tolerance. An empty prediction scores
zero and is retained. CMLt measures continuity in the correct metrical level;
its tolerance differs from both F1 and the APTA phase criterion.

| Metric | Registered minimum | BeatNet model 1 + PF | Beat This! final0, no DBN |
|---|---:|---:|---:|
| Beat F1 | 85% | 74.22% — fail | 89.83% — pass |
| Downbeat F1 | 70% | 45.26% — fail | 78.55% — pass |
| Beat CMLt | 75% | 53.05% — fail | 81.65% — pass |
| Downbeat CMLt | 60% | 32.85% — fail | 67.44% — pass |

These compare a causal reference with an offline model that uses future context.
They are measured accuracy/resource trade-offs, not evidence that the offline
model can replace a causal device component. Per-genre results remain visible:
Beat This! is weaker on classical and metal than on disco/pop, so aggregate
success cannot establish unrestricted genre support or DJ acceptance.

## APTA projection and regressions

Every source contributes all six native S6 windows: **600 total, 401 with
complete reference context and 199 with incomplete boundary context**. The
unchanged dense reference scorer measures each candidate's own fitted grid.
No old meter or reference phase is borrowed. Unknown-context windows are
retained and never counted as completely correct.

| Window metric | Coarse S6 | BeatNet projection | Beat This! projection |
|---|---:|---:|---:|
| Emitted grid, all 600 | 598 | 508 | 560 |
| Confirmed period, complete context | 107 | 85 | 192 |
| Confirmed phase, complete context | 87 | 108 | 300 |
| Confirmed joint period/phase | 47/401 | 80/401 | 187/401 |
| Joint fixes / breaks versus coarse | — | 57 / 24 | 142 / **2** |
| Period fixes / breaks | — | 38 / 60 | 92 / **7** |
| Phase fixes / breaks | — | 71 / 50 | 221 / **8** |

Beat This! improves joint correctness from 11.72% to 46.63% of complete-context
windows. This is a large improvement, but remains insufficient for product
promotion. It also does not prove that all remaining errors are caused by
constant-grid representation: missed/extra events, tempo-family ambiguity,
annotation variation and the specific OLS projection all remain possible.
The preceding reference-fit audit likewise supplies successful witnesses,
not a proof that every failed fit is mathematically unrepresentable.

Meter uses the fixed conservative rule requiring two complete equal bars on
candidate events in the window. BeatNet emits 69/600 meter records, 64 matching
the reference; Beat This! emits 215/600, 195 matching. Downbeat-coordinate
correctness is 41/69 scored records for BeatNet and 167/207 for Beat This!,
with reference-context limitations retained. These conditional counts must
not be presented as whole-corpus meter/downbeat acceptance rates.

Both candidates explicitly lack calibrated confidence. Their grid adapter uses
zero only to avoid advertising a false confidence value. The resulting zero
high-confidence errors do **not** satisfy a confidence-safety qualification.
The original coarse result has thirteen high-confidence grid errors and
thirteen high-confidence emissions with incomplete context.

## Verification, cost and retained evidence

The frozen attempt binds **11550 files**, including an exact 893-file project
source snapshot, selected source/reference identities, installed Python/native
runtime files, model source, weights and unchanged native analyzer. All bound
files are verified before and after scoring. All 200 saved event files are
hash-checked; saved event metrics, projected tuples and all window verdicts
are reloaded and reproduced independently after the first scoring pass.

Ten projection/provenance tests, four event/boundary metric tests and six
unchanged annotated-development tests pass. Synthetic official-network chunk
parity and decoder/reset checks are recorded in the preflight checkpoint.
Rust/C processing and frozen scorer code are unchanged; the complete native
test/build/P4 matrix is not rerun for this host-only tooling step.

| Host measurement | BeatNet | Beat This! |
|---|---:|---:|
| Sum of 100 source inference times | 432.38 s | 388.25 s |
| Slowest source | 7.81 s | 4.26 s |
| Worker wall time, including setup | 439.41 s | 395.28 s |
| Peak process RSS | 466592 KiB | 515824 KiB |

The two CPU workers ran concurrently with two PyTorch threads each. Each
worker loads both reference networks and runs synthetic preflight; RSS is
therefore the **complete host harness**, not isolated model RAM. Python PF cost
and desktop timings must not be extrapolated to native Rust/P4. Both registered
host resource caps pass. No P4 inference, quantization or audio coexistence was
measured.

Beat This! supplies ten beat and three downbeat events exactly at the exclusive
source endpoint across the cohort. All are retained separately in raw records;
none is shifted into the audio domain. BeatNet has none. The failed partial
attempt and first missing-annotation preparation are preserved without scoring
or overwriting them. A mirrored final0 checkpoint is hash-bound with the
provenance limitations documented in the preflight checkpoint.

External evidence root:
`/home/daniel/.local/share/libapta-audio/rust-rewrite/neural-reference-20261022/`.
Successful attempt: `comparison-source-domain/`; raw model output is under each
model directory, with `scoring/report.json` and per-source score records.
The private prepared manifest is under `prepared-availability/`.
Exported aggregates and the exact source archive are under
`D:/AI/LIBAPTA/outputs/rust-neural-reference-20261022/`.

- Prepared manifest SHA-256:
  `47dcabf6c3d683ac2fdb0e5b58f9c60f84644ad4cf24b0558ee872f4aee6510c`.
- Frozen attempt SHA-256:
  `0e0ab33d990e40253d61a6a39758fc30c33510238e02ad58d1467b7fa3d5e3ef`.
- Scoring report SHA-256:
  `9f5832722dfb51a9ff05b535d18f72c7328270f9d7670bfda8df576e192d24be`.
- Exact source archive SHA-256:
  `7b67c736d167f07737456ff128aa3dfb1a138e85aff23ebebe029b39338fdb82`.

GTZAN is excluded by dataset name from both selected checkpoints' declared
training sets. Cross-dataset recording/composition reuse has not been fully
verified, so this is benchmark development evidence, not fully independent
transfer or fresh product acceptance. Formal holdouts and the fresh DJ pool
remain closed. Confidence, key, consumer adoption and physical qualification
remain separate open gates; production defaults, pins, version and release
are unchanged. Commit and push this measured result before a new experiment.
