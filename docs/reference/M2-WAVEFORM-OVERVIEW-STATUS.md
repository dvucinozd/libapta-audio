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
- cleanup of queued PCM, accumulators and snapshot payloads.

## Advertised behaviour

`apta_context_get_capabilities()` returns:

```text
APTA_FEATURE_WAVEFORM_OVERVIEW
```

A session must explicitly request that feature before PCM is retained and analysed.

The current waveform analyser supports push input only. A pull-mode session requesting waveform overview returns `APTA_ERROR_UNSUPPORTED`; this avoids advertising a source path that is not yet implemented.

## Runtime tests

The test suite currently includes:

- core lifecycle and result ownership;
- allocation-failure cleanup;
- cancellation lifecycle;
- public structure initializers;
- incompatible-version rejection;
- sparse overview coverage and golden peak/RMS columns;
- waveform block-boundary determinism;
- focus-region preemption over earlier background PCM;
- unknown-duration PCM demand and EOF boundary contracts.

The sparse overview test has been observed passing in GitHub Actions. The complete latest nine-test package still requires an Actions result for the newest commit before M2 is marked complete.

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

- the latest GitHub Actions run builds all source modules;
- all nine runtime tests pass;
- compiler warnings remain clean under the configured warning level;
- a memory-limit regression test is added;
- a concurrent result acquire/release stress test is added or explicitly deferred with a documented threading limitation;
- destruction concurrency rules are documented;
- this status document is updated with the verified commit SHA.

## Next implementation work

The next bounded sequence is:

1. memory-limit and snapshot-allocation regression tests;
2. result-acquire/release concurrency stress test;
3. public threading and destruction contract;
4. version-1 `WOVR` writer;
5. hardened `WOVR` parser and malformed-input corpus;
6. Waveform Profile conformance report;
7. detail-tile geometry and adaptive retention.
