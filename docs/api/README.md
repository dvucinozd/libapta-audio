# API and ABI documentation

The stable public C API and ABI contract covers:

- ownership and object lifetimes;
- versioned structures and append-only 1.x compatibility;
- fixed-width public types;
- PCM push and pull contracts;
- scheduling and priority-region APIs;
- immutable result snapshots;
- threading and callback rules;
- error and backpressure semantics.

Current contract:

- [`APTA-API-ABI-1.0.md`](APTA-API-ABI-1.0.md) — stable public API and ABI 1.0
  contract, version domains and compatibility rules.

Historical design records retained for audit and implementation context:

- [`APTA-PUBLIC-API-ABI-POLICY-0.1.md`](APTA-PUBLIC-API-ABI-POLICY-0.1.md) —
  original draft compatibility policy;
- [`APTA-THREADING-0.1.md`](APTA-THREADING-0.1.md) — host serialization,
  concurrent immutable readers, cancellation and destruction;
- [`APTA-PCM-PULL-0.1.md`](APTA-PCM-PULL-0.1.md) — callback ownership,
  backpressure and end-of-input behavior for pull sessions;
- [`APTA-SESSION-SEEDING-0.1.md`](APTA-SESSION-SEEDING-0.1.md) — compatibility
  checks and waveform-coverage continuation from a parsed partial result.

The `0.1` suffix on a historical design record identifies the document's
origin, not the current package or API version. The stable public authority is
the 1.0 contract, installed headers, ABI manifests and tagged release evidence.
