# H0: historical real-runtime metadata audit

Date: 2026-10-25. **H0_METADATA_AUDIT_COMPLETE**; independent closeout PASS.
**Real execution readiness remains NOT_READY.**
[Aggregate receipt and exact identities](REAL-RUNTIME-H0-20261025.json).

The [metadata-only scope](REAL-RUNTIME-H0-PROTOCOL.md) and
[registration](REAL-RUNTIME-H0-PROTOCOL.json) were committed/pushed at clean
`43445ad7890da7b2deb48bfe5ef624364da77441`, matching origin/remote before audit.
Registration SHA-256:
`777e8209845d37b9190f956dadb53c53b16b940f4391e91f710a40c00f65663b`.
No DSP, model, package, upstream, historical freeze or system file is changed.

## Findings

Across the six selected metadata roots, the audit compares **11362 unique
permitted program files**. **11361 match; one differs: `libz.so.1.3`.** There are
no conflicting historical expectations among the selected runtime paths.
Observed program identities remain stable across both audit passes and a third
independent closeout pass. All six metadata files remain unchanged.

| Metadata view | Compared programs | Equal | Changed | Independently anchored root |
| --- | ---: | ---: | ---: | --- |
| Original neural-comparison freeze | 10449 | 10449 | 0 | yes |
| Beat This worker metadata | 34 | 34 | 0 | yes |
| Dense-capture freeze | 10449 | 10449 | 0 | no, as-of registration |
| DD-05 construction completion | 0, identity links only | — | — | yes |
| DD-05 construction freeze | 127 | 126 | 1 | yes, via completion |
| DD-05 evaluation freeze | 1629 | 1628 | 1 | no, as-of registration |

These are overlapping views, not additive file counts. Four roots have an
independently recorded hash or selected-root parent chain; the other two retain
their weaker as-of identity. No scoring report was opened to strengthen that
attestation. Their matching asset expectations can still be compared with the
independently anchored original freeze; this does not attest every metadata field.

The real DD-05 construction freeze independently records zlib digest
`86200da370f20476a2507e9097a789b5ef97269b4ca8d5e164ad82dab9d99892`.
Retained evaluation metadata records the same digest. Current host file
`/usr/lib/x86_64-linux-gnu/libz.so.1.3` is 113000 bytes with digest
`f61677c7bdb7c2aa2a243ea781bfd16afcb361cede7b4ef76127a9d8eb9a5242`.
Thus the previously documented generated-run library delta also breaks byte
identity for a retained **real decoder** runtime. This finding is not inferred
from version strings or from the generated runtime alone.

The original neural and dense-capture freezes bind installed/upstream programs
but do **not** bind the interpreter or external system-runtime files in this
comparison. Their absence of a zlib entry is a coverage gap, not evidence of an
unaffected/reproducible inference runtime. No model was loaded, so numerical
effect on model output is untested. Historical results remain frozen with their
recorded evidence; this audit does not relabel them INVALID or successful.

Current upstream/site-package enumeration finds **9457 program filenames not
bound by any selected historical root: 9433 `.pyc` caches and 24 shared-library
files**. These are unrecorded importable candidates, not proven new installations
or necessarily dependencies of a particular load path. Their contents are not
hashed by this audit. They must receive an explicit membership/identity decision
in a new runtime qualification. Project-source evolution, protected data and
weights are excluded, not counted as runtime drift.

## Verification and limits

Four public checks pass with zero failures/errors/skips: filename classes and
denied data/weight suffixes, separate equal/changed/missing/symlink observations,
metadata identity links, and malformed binding rejection. These execute before
historical contents open. The external inspector uses existing stdlib hash/
binding/deadline/supervision helpers and an explicit content-open guard. It never
imports a model/scientific module or executes the historical venv.

Independent read-only closeout reconstructs the selected set, per-root classes,
expected identities, equality/drift counters and provenance propagation; rehashes
all 11362 allowed programs and six metadata roots; and independently enumerates
the unrecorded filename set. Audit runtime/source and old metadata rechecks pass.
Protected audio, labels, events, scores, logits and weights stay unopened.
There are zero inference, training or scoring calls and no package operations.

Audit wall time 37.253 s, self peak RSS 50764 KiB; supervision 37.573 s,
maximum observed process RSS 51132 KiB. Closeout wall time 32.135 s, self peak
57776 KiB; supervision 32.539 s, observed peak 57776 KiB. Both inspectors have
a hard 268435456-byte AS ceiling. Registered 1800 s/process, 60 s/program hash,
262144 KiB/process and 67108864 new external bytes are unchanged and respected.
These are metadata/hash costs, not neural latency or model/product memory.

Concrete source is the clean registration commit above. The bound external
`audit_runtime.py` runs with system Python, `-B -X pycache_prefix=/libapta-no-cache`,
the registration path/hash and source SHA. It observes the **live host** bytes,
not paths overlaid by the generated runtime image. Its own loaded runtime is
bound/rechecked. `closeout.py` performs the independent metadata/count/hash pass.
Private paths/maps/per-file evidence, launch, scripts and resource logs remain
under `D:\AI\LIBAPTA\outputs\real-runtime-h0-20261025`. No old data/program
tree is copied into that output. Aggregate identities/counts only enter Git.

## Decision and next step

H0 is now **AUDITED_WITH_DRIFT_AND_COVERAGE_GAPS**, replacing NOT_AUDITED for this
bounded metadata task. A new real run remains NOT_READY: there is a verified
bound-library mismatch, incomplete original loader/cache coverage, partial root
attestation and no new numerical/execution qualification. Do not update expected
hashes in the old freezes, patch packages, disable security updates or replay the
old corpus/56-source subset to bypass this result.

Next H1: separately register a new immutable **real neural-runtime** preparation
and public/synthetic compatibility checkpoint. Bind the selected unchanged
model/checkpoint/upstream source, interpreter, caches, extension/ELF dependencies
and symlink endpoints; keep program/weight input leaves separate from protected
data and never snapshot the entire mixed experiment directory. Prepare read-only
execution against the new identity and reuse the canonical public chunk/reset/
postprocessing checks under registered neural-host limits. H0's metadata AS/RSS
figures are not inference budgets. Distinguish internal consistency from numerical
parity with an old runtime when no retained golden output exists.

Only a qualified new runtime plus an explicit scientific/projection-scope decision
could support a separate real-scope proposal. H1 is not registered or executed
here. Original model/decoder/meter results and every rejection remain unchanged;
there is no automatic real replay, candidate promotion, transfer, confidence,
native/consumer activation, C/Rust/P4 qualification, fresh acceptance or release.
Independence remains UNRESOLVED; whole-pipeline product budgets UNKNOWN.
VERSION remains 1.0.1.
