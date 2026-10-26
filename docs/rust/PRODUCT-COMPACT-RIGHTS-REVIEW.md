# Compact candidate rights and training-provenance review

Date: 2026-10-26. Source baseline:
`23827c5936e5670a7b5a178533fc07e3ac0acd69` on `rust-rewrite`.
[Machine receipt](PRODUCT-COMPACT-RIGHTS-REVIEW.json).

## Finding and scope

The public evidence reviewed here does **not close LICENSE_UNRESOLVED or
INDEPENDENCE_UNRESOLVED** for the proposed BeatNet+ main-branch checkpoint.
This is a bounded evidence review, not a conclusion that no separate grant can
exist. The original [binding receipt](PRODUCT-COMPACT-BINDING.json) is frozen;
its finite-value PASS and the HS-01 synthetic host result remain unchanged.

The repository's observed `main` still resolves to
`bb90eb0a9065b101a4b4c4cb2b2061950266cb4b`. The complete, untruncated Git tree
still identifies `src/BeatNetPlus/models/generic_main_weights.pt` as blob
`7aefd5e56ef65482be6f671c3cca8de457dad102`. Its previously bound SHA-256 is
`5bbae630b4112f3c1193654e3dbc946b850caa06a2a82e10e8de9e48bb673519`.
This review neither downloaded nor opened that weight file.

## Public evidence examined

| Evidence | Observation | What it establishes |
|---|---|---|
| [Repository metadata](https://api.github.com/repos/mjhydri/BeatNet-Plus) and pinned full tree | API license is null; no named LICENSE/COPYING/NOTICE/model-card file | No affirmative artifact grant found in this scope |
| [Pinned setup.py](https://github.com/mjhydri/BeatNet-Plus/blob/bb90eb0a9065b101a4b4c4cb2b2061950266cb4b/setup.py) | No package license declaration | Package metadata does not close the gap |
| [License issue #4](https://github.com/mjhydri/BeatNet-Plus/issues/4) and both comments | Open request to add a license; both comments are from non-owners | No maintainer grant in that thread at inspection |
| [Availability issue #1](https://github.com/mjhydri/BeatNet-Plus/issues/1#issuecomment-4315082821) | Owner confirms code and weights exist | Availability only; no use/distribution terms |
| [Annotation issue #2](https://github.com/mjhydri/BeatNet-Plus/issues/2#issuecomment-4323680312) | Owner announces planned annotation availability | No checkpoint-specific training manifest |
| [Pinned README](https://github.com/mjhydri/BeatNet-Plus/blob/bb90eb0a9065b101a4b4c4cb2b2061950266cb4b/README.md) and [generic config](https://github.com/mjhydri/BeatNet-Plus/blob/bb90eb0a9065b101a4b4c4cb2b2061950266cb4b/src/BeatNetPlus/configs/generic.yaml) | Training-family declarations and GTZAN test-only declaration | Declared recipe, not verified membership of this exact weight artifact |
| [Publisher article, DOI 10.5334/tismir.198](https://transactions.ismir.net/articles/10.5334/tismir.198) | Article carries CC BY 4.0; Table 1 distinguishes training/validation from GTZAN testing | Article terms and reported experimental setup; no explicit grant binding the 2026 uploaded artifact |
| [Repository releases](https://api.github.com/repos/mjhydri/BeatNet-Plus/releases?per_page=100) | Empty list | No release-specific grant or manifest found there |

The publisher identifies the paper as *BeatNet+: Real-Time Rhythm Analysis for
Diverse Music Audio*, by Mojtaba Heydari and Zhiyao Duan, published 2024-12-06.
The pinned README citation has different title/author metadata. Preserve that
discrepancy; neither the paper citation nor the upload date proves which exact
training run produced the bound checkpoint. No alternate model was selected.

The paper and config describe Ballroom, Hainsworth, Rock Corpus, MUSDB18,
URSing and RWC Pop/Jazz/Royalty-free as training/validation families, with GTZAN
for testing. Treat these as overlap risks for later evaluation. Our GTZAN use
is already exposed development, irrespective of the upstream test-only claim.
Unknown recording/performance membership stays UNKNOWN. No formal holdout,
annotation file, source audio or saved neural output was opened.

## Concrete evidence needed to close the two gates

For rights, obtain an affirmative grant identifying the code revision and exact
checkpoint digest above, the rights holder and applicable use/modification/
redistribution terms for the intended firmware, including attribution and
third-party obligations. Terms for the paper, original BeatNet, dependencies or
a downstream port do not establish this scope by themselves.

For provenance, obtain a checkpoint-specific training record: run ID/date,
code/config digests, initialization or fine-tuning lineage, actual train/validation/
test/selection splits and dataset versions, seed, preprocessing/augmentation and
recording/performance identities sufficient for overlap checks. Private membership
lists must stay outside Git. Bind the record to the exact checkpoint SHA-256;
then check proposed development/transfer membership and duplicates separately.
Receiving a manifest alone is not an independence PASS.

These paragraphs define a reviewable information request; **nothing was sent**.
No email, GitHub issue/comment or other external message was created. This task
authorizes public inspection, not contacting the authors.

## Receipts and next boundary

External snapshots are under
`D:/AI/LIBAPTA/outputs/product-compact-rights-review-20261026/`.
The JSON receipt binds URL/snapshot identities, complete tree and comment counts,
normalization details and the source baseline. GitHub connector content snapshots
have one final LF; raw Git blob identity is checked with the original EOF variant.
The publisher HTML is retained separately. The review is not an exhaustive search
of private agreements or unpublished training records.

No DSP/runtime/consumer code, weights, packages, model execution, training,
corpus, device or release changed. VERSION remains 1.0.1. PF-01 remains
NEEDS_MEASUREMENT because JC1060P470C_I_W_Y is unavailable; silicon revision is
unobserved. Product PF-04 remains NOT_RUN.

The next permitted deliverable is a separate synthetic path-to-events-to-grid
registration based on the existing CS-01/PF-02 math. It can specify and test
artificial paths/probabilities without these neural artifacts or physical hardware.
It must not change the model, old verdicts, product defaults or acceptance gates.
