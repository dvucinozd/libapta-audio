# API and ABI documentation

Design documents for the public C API and ABI, including:

- ownership and object lifetimes;
- versioned structures;
- fixed-width public types;
- PCM push and pull contracts;
- scheduling and priority-region APIs;
- result snapshots;
- threading and callback rules;
- error and backpressure semantics.

Current documents:

- [`APTA-PUBLIC-API-ABI-POLICY-0.1.md`](APTA-PUBLIC-API-ABI-POLICY-0.1.md) —
  source and binary compatibility policy for the draft public headers.
- [`APTA-THREADING-0.1.md`](APTA-THREADING-0.1.md) — host serialization,
  concurrent immutable readers, cancellation and destruction.
- [`APTA-PCM-PULL-0.1.md`](APTA-PCM-PULL-0.1.md) — callback ownership,
  backpressure and end-of-input behavior for pull sessions.
- [`APTA-SESSION-SEEDING-0.1.md`](APTA-SESSION-SEEDING-0.1.md) — compatibility
  checks and waveform-coverage continuation from a parsed partial result.
