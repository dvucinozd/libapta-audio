# Beat/downbeat model decision — 2026-10-22

Research baseline: `rust-rewrite` at
`13256e6c11fba4ed88958ccf1d767e32b62aff3c`, synchronized with origin.
This is a source/literature audit and engineering recommendation, not a new
corpus result, model adoption, or ESP32-P4 qualification.

Measured follow-up: the [fixed 100-source comparison](NEURAL-REFERENCE-RESULT-20261022.md)
now supersedes the provisional P4 candidate below. Beat This! passes all four
event/continuity screening gates; original BeatNet fails all four and its port
is rejected under that protocol. Neither qualifies for production/P4. Next is
event-to-APTA representation analysis, with Beat This! as the fixed reference.

## Pre-measurement decision

Stop adding handcrafted selectors over the existing onset/energy evidence.
Use **learned beat and downbeat activations**, with:

- **BeatNet, original two-layer causal CRNN**, as the first standalone-P4
  candidate. It offers a small, stateful network, available weights and a
  declared attribution license. Start from official `model_1_weights.pt` in
  online/PF mode; this is a fixed engineering choice, not a winner selected
  after observing our labels.
- **Beat This! `final0`, without DBN**, as the fixed offline quality reference.
  Its role is to reveal how much error comes from musical evidence versus our
  output representation. It is not the proposed P4 runtime.
- **BeatNet+** as a conditional upgrade, not an additional immediate sweep.
  It is technically attractive, but the audited upstream tree contains no
  license file or explicit README grant for code/weights. Resolve that before
  importing or redistributing it. Original BeatNet's license cannot simply be
  applied to this separate repository.

This is the best practical direction among the reproducible options reviewed,
given standalone P4 and native Rust constraints. No reviewed model has yet
demonstrated all our accuracy, confidence, resource and integration gates.
If analysis is explicitly moved to a desktop preprocessing workflow, Beat This!
becomes the preferred implementation candidate instead; that is a product
architecture change, not an assumed dependency of the standalone device.

## Why the previous iterations did not converge

The [WP1 closure](../status/APTA-1.1-WP1-CLOSURE.md) records eight rejected
handcrafted evidence iterations. The [WP2 closure](../status/APTA-1.1-WP2-CLOSURE.md)
records three continuity attempts without sufficient independent transfer.
These were not repeated training runs of competitive neural beat models.

The native sparse candidate reached 36/36 generated cases but emitted 0/360
supported grids on spent recorded inputs. Energy novelty found events in every
window but still emitted 0/360. The event-lattice selector reduced joint-correct
windows from 95/360 to 16/360. The latest
[dense temporal attempt](S6-TEMPORAL-DEVELOPMENT.md) improved ASAP joint
correctness from 1/160 to 4/160 complete-reference windows, broke two previously
phase-correct windows, and stopped before Ballroom under its frozen veto.

Four correct windows out of 160 is 2.5%; relaxing the two-break veto would not
make the model adequate. The evidence points toward a missing distinction
between musical beats and other attacks, alongside a possible representation
limit. It does not prove that a neural replacement will meet the product gates.
Preserve all failed results and thresholds.

## Shortlist and evidence

| Option | Published/source evidence | Resource and deployment assessment | Decision |
|---|---|---|---|
| Beat This! main / small | GTZAN paper beat/downbeat F1: main 89.1/78.3; small 88.8/77.2, means over three seeds. MIT explicitly covers published code and weights. | About 20M / 2M parameters; checkpoints about 78 MB / 8.1 MB. Offline context; model size alone does not describe runtime RAM. | Main `final0` is the quality reference; no seed/model sweep. |
| BeatNet original | Causal CRNN plus particle decoding; beat/downbeat/tempo/meter. Upstream CC BY 4.0; bundled weights, no separate weight-license file found. | Derived 402,325 parameters; 1.61 MB raw FP32 weights, about 20.25 million MAC/s at 50 feature frames/s, before frontend/decoder. | First P4 candidate; actual quality and device execution remain unmeasured. |
| BeatNet+ | Authors report GTZAN online beat/downbeat F1 80.62/56.51 versus original BeatNet 75.44/46.69 in that comparison. Training adds non-percussive evidence; inference uses one branch. | Derived 767,125 parameters; 3.07 MB raw FP32 weights and about 38.39 million MAC/s before frontend/decoder. License unresolved. | Conditional upgrade after license resolution and a new registered comparison. |
| BEAST | Published streaming transformer with under-50-ms reported latency; beat/downbeat tracking. | More involved transformer port; no local P4 evidence. | Does not displace the smaller, simpler CRNN as the first engineering candidate. |

The offline and online scores are **not an apples-to-apples product ranking**.
Context, training data, decoding and timing constraints differ. Paper F1 also
does not mean the percentage of fully correct APTA grids. Beat This!'s authors
explicitly identify continuity weaknesses; DJ synchronization needs continuity
and tempo-family correctness as well as event F1.

Primary sources: [Beat This! paper](https://arxiv.org/html/2407.21658v1),
[Beat This! code/checkpoints/license](https://github.com/CPJKU/beat_this/tree/b95c8ab0c58c2d9fcfd40508ae8dffbc05ac4f5c),
[BeatNet paper](https://arxiv.org/abs/2108.03576),
[BeatNet source/license](https://github.com/mjhydri/BeatNet/tree/81cedd4beeb7235262db80969a0c9ce9a48a0ed4),
[BeatNet+ author implementation and results](https://github.com/mjhydri/BeatNet-Plus/tree/bb90eb0a9065b101a4b4c4cb2b2061950266cb4b),
[BeatNet+ paper](https://transactions.ismir.net/articles/10.5334/tismir.198),
[BEAST paper](https://arxiv.org/abs/2312.17156).

The recursively inspected BeatNet+ tree was complete and had no LICENSE,
COPYING or NOTICE path; README and setup.py supplied no license grant either.
A community C++ port claims non-commercial weights, but that is not an
authoritative upstream grant. Do not describe either commercial permission or
an upstream non-commercial restriction as established by this audit.

Other reviewed directions do not justify another immediate branch of work:
[BTrack](https://github.com/adamstark/BTrack) is beat-only and GPLv3;
[madmom](https://github.com/CPJKU/madmom/blob/main/LICENSE) separates BSD source
from non-commercial/share-alike models. The new
[CASM preprint](https://arxiv.org/abs/2610.09318v1) improves decoding of already
learned activations; it is a later continuity option, not a substitute for
obtaining better activations first.

## Native Rust and P4 cost

The [Beat This! Rust port](https://github.com/danigb/beat-this-rs/tree/1ae768e78f1ad83b0ed3886241dc29ffde853c40)
uses RTen by default and optionally ONNX Runtime. Its streaming API accumulates
features for final analysis. The author reports about 620 MiB peak RSS for a
one-hour full-model run on an Apple M4 Pro. That is a host measurement, not a
P4 estimate; neither this runtime nor a small checkpoint establishes bounded
embedded streaming operation.

[beatnet-rs 0.1.0](https://docs.rs/crate/beatnet-rs/0.1.0) provides a useful
streaming/frontend/decoder reference, but uses ONNX Runtime through `ort`.
It is not a ready `no_std` P4 inference engine. Verify parity against the
official model before treating a community conversion as reference truth.
Preserve explicit source coordinates, discontinuity reset and hidden/cell
state across chunks. Centered feature windows introduce lookahead; causal
network state does not imply zero end-to-end latency.

The resource estimates above are derived from the inspected model definitions,
not measured timings. For input dimension D, L recurrent layers and H=150:

```text
conv_positions = D - 10 + 1
flattened = 2 * floor(conv_positions / 2)
parameters = 22 + (flattened * 150 + 150)
             + L * (4 * 150 * 150 + 4 * 150 * 150 + 8 * 150)
             + (150 * 3 + 3)
MAC/frame = 2 * conv_positions * 10 + flattened * 150
            + L * 4 * 150 * (150 + 150) + 150 * 3
```

Original: D=272, L=2; Plus: D=288, L=4. Raw recurrent h/c storage is
2,400 / 4,800 bytes FP32. These are **not total RAM budgets**: FFT/filterbank,
feature history, activations, particle state, allocator/runtime, stacks,
weights placement and integration buffers must be included. Integer weights
do not automatically imply an integer-only LSTM or preserved accuracy.

Current [ESP-DL operator support](https://github.com/espressif/esp-dl/blob/eb72563e6a4e61d2bbc0d46e4c79f2c9f816c3c6/operator_support_state.md)
includes LSTM in int8, int16 and FP32, but not w8a16; operator support is not
proof that a complete exported graph runs correctly or meets our budget.
The [LSTM implementation](https://github.com/espressif/esp-dl/blob/eb72563e6a4e61d2bbc0d46e4c79f2c9f816c3c6/esp-dl/dl/module/include/dl_module_lstm.hpp)
has lazy allocations and explicit initial-state handling. A backend would
need initialization/warmup outside processing, retained state and verified
reset behavior. ESP-DL is C++, so adopting it would be an explicit optional
platform backend, not silently replacing the native Rust core. First establish
reference correctness; then measure a bounded inference implementation on P4.

## Evaluation traps to remove before another run

1. **Training overlap.** Beat This! `final*` and `small*` include ASAP and
   Ballroom training data and exclude GTZAN. Our existing ASAP/Ballroom split
   does not make those checkpoints independent; re-rendering a composition
   does not resolve overlap. Original BeatNet's inference source marks models
   1/2/3 as GTZAN-out/Ballroom-out/Rock-Corpus-out respectively. The README's
   shorter dataset labels must not be interpreted as training only on that
   dataset. Verify exact checkpoint provenance and performance/composition
   membership before independent-transfer claims.
2. **Representation.** S6 currently publishes a constant-period grid over an
   approximately 5.46-second window. A correct sequence of expressive beats
   can fail its 1% period and 0.10-beat whole-window phase criteria. Register a
   label-only representability audit on already-open development references;
   report failures separately from event detection errors. Do not use a
   label-derived grid as candidate output or an acceptance result.
3. **Metric mismatch.** Report event beat/downbeat F1 and continuity separately
   from the unchanged APTA scorer, including half/double-time errors, meter on
   the candidate's own grid, abstentions and high-confidence errors. The
   literature's event tolerances cannot replace the product gates.
4. **Domain.** Expressive solo piano is a valuable stress stratum, but its
   result alone cannot select a DJ model. Keep a registered DJ-relevant
   development stratum and stress results separate. Do not retroactively
   change the failed temporal experiment's ASAP-first rule.

## Bounded next step and stop conditions

Prepare **one two-model host reference protocol**, using original BeatNet
model 1 + PF and Beat This! final0 without DBN. Before reading new labels:

1. Bind source/weight/conversion hashes, license notices, training membership,
   sample-rate conversion, timestamp origin/lookahead, state reset, supported
   tempo/meter ranges and fixed decoder settings. A public GTZAN development
   probe is a possible shared excluded dataset, subject to rights and
   duplicate/overlap verification; it is not fresh product acceptance.
2. Register data and numeric decision gates, resource caps and the
   representation audit. Existing ASAP/Ballroom data may diagnose mechanics
   but cannot establish independence for a model trained on them. Keep formal
   holdouts and the fresh >=48-track acceptance pool closed.
3. Verify inference/serialization parity and synthetic timestamps/gap resets,
   then run the fixed references once. Report raw events, APTA projection and
   confidence separately; do not select seeds, thresholds or checkpoints after
   seeing results. Include absolute quality, fixes/breaks and uncertainty.
4. If BeatNet passes the registered independent-development gates, proceed to
   a bounded Rust inference implementation and physical P4 profiling. If
   event quality fails, stop the port. If events are adequate but projection
   fails, address the representation before changing the network. If only the
   offline reference succeeds, report that a viable standalone-P4 model has
   not been found; consider a separately authorized architecture/training step.

No new training campaign or BeatNet+ import is authorized by this research
decision. Production provider, consumer pin, stable version, release and all
frozen qualification criteria remain unchanged. Musical key is a separate
unfinished evidence problem; beat/downbeat models do not close its gate.

## Verification of this checkpoint

Only public papers, source, license files, repository metadata and existing
tracked project results were inspected. No model was executed, no private or
fresh acceptance audio was opened, and no P4 result is claimed. Parameter/MAC
arithmetic and Markdown relative targets are checked locally; DSP tests are
not rerun for this documentation-only checkpoint. Commit and push this decision
before starting the separately registered implementation/evaluation step.
