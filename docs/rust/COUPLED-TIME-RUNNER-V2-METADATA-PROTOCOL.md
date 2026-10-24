# Coupled-time metadata runner registration

Date: 2026-10-24. **C0 only; no implementation, preflight or authorization.**
The [machine registration](COUPLED-TIME-RUNNER-V2-METADATA-PROTOCOL.json) binds
the [support amendment](COUPLED-TIME-SUPPORT-METADATA-AMENDMENT.md),
[new operational schema](COUPLED-TIME-RUNNER-V2-METADATA-SCHEMA.json),
[support proof schema](COUPLED-TIME-REFERENCE-SUPPORT-SCHEMA.json),
old CT03/v2 authorities and the exact saved R4/R5 evidence.
The [C0 receipt](COUPLED-TIME-METADATA-C0-20261024.json) records registration checks
only. It cannot qualify a runtime implementation or a corpus.

## Isolated future implementation

Use `tools/apta_1_1_coupled_time_v2_metadata.py` as the new explicit runner entry,
with a versioned evaluator and pure support helper named in the amendment.
Preserve the original25 R4 component bytes, including its combined runner,
orchestrator, bootstrap and runtime helper. No old13 immutable dependency is edited.
Copy only the required orchestration into the versioned entry; keep numerical
work, source/global checks, isolation, resource supervision and canonical scoring.
No unrelated refactor, global monkeypatch, dynamic unregistered implementation
selection or environment/CLI production override belongs to C1.

Use separate metadata tests, bootstrap, generated driver and fixed runtime launcher.
The launcher may reuse the existing immutable-runtime Python API, with the exact
retained image; it cannot add arbitrary-command or real-input CLI options. New
implementation paths and later preflight component hashes are specified in JSON.
No code at these paths exists or is activated by this registration.

## Records and support proof

Every context, worker, phase completion, supervision, invalid marker, preflight,
authorization, scientific payload and support proof has a new metadata format
and binds `metadata_amendment_sha256`. Runtime semantic checks also match the
scientific parent and current operational registration identities. New source
records cannot borrow old v2/CT05 envelopes or authorization.

Construct/verify contexts and workers carry authority markers only, not reference
counts, labels or support proofs. Non-evaluation completions have null
`reference_support_sha256` and no reference import. Evaluation completion binds
both the scientific payload SHA and the support proof SHA. The payload binds that
same proof; the proof binds the evaluation context and original input/freeze/
verification chain. Header roles, indices, source/window counts and all digests
must agree with actual artifacts, not just individually satisfy a schema.

Context/worker/candidate relative filenames may stay familiar inside a new
exclusive attempt root, but format/identity validation must reject mixed content.
Metadata supervision and final evaluation filenames are distinct; see `record_paths`.
The new preflight and real authorization filenames are distinct from every old
token, and neither file is created at C0.

The closed proof includes an ordered source list with ordered window indices,
integer complementary counters and source/cohort totals. Require exact contiguous
indices, actual manifest geometry/counts and sum equality at every level. Validate
support identity against the expected authority and evaluation context. The schema
does not perform arithmetic or cryptographic linkage for the implementation.

The new scientific payload preserves all old numeric/semantic fields and declares
its new format and authority/proof fields explicitly. Fixed counter key sets are
closed; old raw-score groups are not given the new field. Per-source parity may
strip only the six exact registered leaf paths, never arbitrary metadata or old
context fields. Cohort comparison retains all old scientific values while checking
the deliberately new source/artifact identities against their actual hash chain.

## Future generated qualification and stop policy

Reuse the fixed original arithmetic100-source/six-window generator and original
runtime image/storage classes. New generated baseline support is400/200=600;
old archived clones/results are unchanged. Require100/100 candidate-byte parity,
all600 legacy diagnostic records equal after permitted additions are removed,
all event/family/paired values and16 gates identical and REJECTED (14/16) preserved.
Neither a support bug nor a metadata change may hide the projection regression.

Retain60s/source,1800s/phase,262144KiB/process and67108864-byte attempt output.
Keep full parent source/cohort checks, selected-only worker access, fresh workers,
independent verification, physical label withholding and unchanged canonical scoring.
New proof/metadata work remains inside evaluation supervision; its cost is not
deducted from the limit. Runtime image/setup costs remain separately reported inputs.

Pass only after the required generated suites and fresh full construct100,
verify100 and evaluate100/600 complete on clean pushed code. Tests and exact
future component identities belong to the new preflight. An integrity, resource,
support or parity failure closes the new attempt INVALID, preserving partials;
no automatic retry, parameter rescue, reduced subset or cap increase follows.

## Real execution remains a later decision

A future real authorization must bind the new metadata preflight, actual source,
scientific/amendment/operational identities, fixed original input hashes, new
destination, prior failed CT05 identity and one-run scope. It additionally binds
an explicit projection-scope decision and authorized historical real-runtime
metadata audit. These reports need validated content/status, not merely SHA strings.
No real authorization is possible by reusing the old tokens or setting support
expectations to zero. Independence stays UNRESOLVED; any later complete passing
real development result can only be EXPOSED_DIAGNOSTIC_PASS_ONLY.

The current C0 receipt is schema/contract evidence, not C1 implementation, C2
runtime validation or C3 preflight. Next is C1 under this registration, followed by
the frozen ordered repair plan. P4, native adoption, confidence, fresh acceptance
and release remain separate. No real data, candidate, inference or scoring is run
as part of this document/schema checkpoint.
