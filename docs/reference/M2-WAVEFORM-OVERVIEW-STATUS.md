# M2 waveform overview status

**Status:** Implementation candidate  
**API version:** 0.1.0 draft  
**Advertised capability:** `APTA_FEATURE_WAVEFORM_OVERVIEW`  
**Profile conformance:** Not yet claimed

## Implemented

The current M2 implementation provides:

- push-mode PCM input for mono and stereo sources;
- S16, packed S24 little-endian, S32 and F32 input conversion;
- deterministic stereo reduction using `(L + R) / 2`;
- copied, bounded PCM queue nodes;
- partial push acceptance and `APTA_STATUS_MORE_WORK`;
- overlap rejection for already accepted source ranges;
- out-of-order, non-overlapping PCM delivery;
- explicit tracking of sparse accepted ranges;
- background PCM demand that preserves earlier gaps;
- focus- and request-priority queue selection;
- bounded cooperative processing by frame and step budget;
- fixed overview geometry of 1024 source frames per column;
- sparse per-column min, max and RMS accumulators;
- normatively specified ties-to-even quantization;
- explicit clipping flags;
- explicit coverage spans without fabricated gap columns;
- immutable overview payloads in result generations;
- partial, stable and final overview lifecycle states;
- range-scoped feature-state queries;
- final partial-column handling after end-of-input;
- cleanup of queued PCM, accumulators and snapshot payloads;
- measurable minimum and recommended memory requirements;
- context memory-limit enforcement;
- concurrent immutable result readers during publication;
- cross-thread cooperative cancellation;
- explicit session/context destruction concurrency rules.

## Advertised behaviour

`apta_context_get_capabilities()` returns:

```text
APTA_FEATURE_WAVEFORM_OVERVIEW
```

A session must explicitly request that feature before PCM is retained and analysed.

The current waveform analyser supports push input only. A pull-mode session requesting waveform overview returns `APTA_ERROR_UNSUPPORTED`; this avoids advertising a source path that is not yet implemented.

Static workspace mode also returns `APTA_ERROR_UNSUPPORTED` until a real workspace allocator exists; the implementation no longer accepts and silently ignores that configuration.

## Runtime tests

The test suite currently includes twelve runtime tests:

- core lifecycle and result ownership;
- allocation-failure cleanup;
- cancellation lifecycle;
- cross-thread cancellation visibility;
- public structure initializers;
- incompatible-version rejection;
- sparse overview coverage and golden peak/RMS columns;
- waveform block-boundary determinism;
- focus-region preemption over earlier background PCM;
- unknown-duration PCM demand and EOF boundary contracts;
- reported memory requirements and enforced memory limits;
- concurrent immutable-result readers during repeated waveform publication.

The sparse overview test has been observed passing in GitHub Actions. The complete latest twelve-test package still requires an Actions result for the newest commit before M2 is marked complete.

## Threading contract

The public prototype threading rules are documented in [`../api/APTA-THREADING-0.1.md`](../api/APTA-THREADING-0.1.md).

Result acquire/access/release operations may run concurrently with publication. Mutating session calls remain host-serialized except for the explicitly thread-safe cancellation request and query functions.

Session destruction must not race with any operation receiving the same session pointer. Acquired immutable results may outlive their session, but the context remains busy until those results are released.

## Deliberate limitations

The implementation does not yet provide:

- waveform detail tiles;
- three-band waveform values;
- pull-mode analysis;
- source formats with more than two channels for waveform analysis;
- deadline ordering within equal priority;
- starvation-prevention aging;
- static-workspace-only operation;
- `.apta` serialization or parsing;
- tempo or beatgrid analysis;
- stable API or ABI guarantees.

## Conformance position

No formal APTA profile is claimed yet.

Although the implementation now covers most behavioural requirements of the Waveform Profile processing path, profile conformance also requires the normative `.apta` `WOVR` reader/writer and the complete applicable conformance suite.

The Adaptive Waveform Profile is not claimed because detail tiles, starvation prevention and measured adaptive scheduling criteria are incomplete.

## M2 completion gates

M2 can be marked complete when:

- the latest GitHub Actions run builds all source and test modules;
- all twelve runtime tests pass;
- compiler warnings remain clean under the configured warning level;
- this status document records the verified commit SHA.

Race-detector, sanitizer, 32-bit ABI and malformed-container jobs remain required before stable API or profile-conformance status, but they do not block the bounded M2 processing milestone.

## Next implementation work

After the latest M2 Actions run is verified, the next bounded sequence is:

1. version-1 `WOVR` writer and CRC32C support;
2. hardened `WOVR` reader with configured limits;
3. malformed-container regression corpus;
4. writer/reader round-trip golden fixtures;
5. Waveform Profile conformance report;
6. detail-tile geometry and adaptive retention;
7. equal-priority deadline ordering and starvation-prevention aging.
