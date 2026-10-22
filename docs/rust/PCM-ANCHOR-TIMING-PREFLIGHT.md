# PCM-anchor implementation and synthetic preflight

Date: 2026-10-22. The [protocol](PCM-ANCHOR-TIMING-PROTOCOL.json) was committed
and pushed at `e2c0e93030a670994c71cb80338523b5668296f8` before implementation
or audio inspection. Its SHA-256 remains
`0668dc34481a8c6021ea972866ed02d66f763694c8dc2607475143f5febc72de`.

`tools/apta_1_1_pcm_anchor_timing.py` implements the single registered energy-rise
measurement. It reads canonical PCM16 directly, forms an int64 stereo-square
energy prefix and tests 41 positions per anchor. Complete stereo energy remains
within int64 even at maximum PCM16 magnitude for 30 seconds. The refiner receives
only PCM, event identities, source/window geometry and declared gaps. Truth and
baseline decisions enter only the separate scoring runner.

Every event retains its ordinal, downbeat association and original S6 membership.
No-attack, low-energy/contrast, ambiguous, search-edge, ownership and source-edge
verdicts retain the original coordinate with an explicit reason. Known gaps
produce unavailable-input status and affected-window projection abstention.
The projected candidate uses the original OLS and same-grid meter functions
unchanged, including their existing support/tempo boundaries. It never chooses
between candidate and baseline by a reference score.

The runner refuses dirty code or existing output. Before execution it binds all
tracked source, original reports/events/labels, every full canonical WAV, the
Python executable, WAV module and installed NumPy Python/shared-library files.
It writes a private freeze before candidate scoring, processes one source at a
time, reproduces all saved control tuples and scores, retains all 600 windows,
hashes every detail and rechecks inputs/source after the run. Transfer remains
closed until all stage-A gates pass; this runner performs no model inference.

Verification passes **21 new synthetic tests**: exact step timing, opposite
stereo polarity, worst-case int64 energy, silence/steady magnitude, equal attacks,
weak/small rises, source/search/window/midpoint boundaries, input gaps, offsets,
event and meter identity, no missing/extra/half/double repair, WAV geometry and
truncation, invalid/capacity inputs, immutable source arrays, veto and protocol
hash checks. The prior 18 multi-beat, ten representation, ten neural-reference
and six annotation tests also pass, for **65 total Python tests**.

```sh
# Explicit host experiment environment; existing NumPy 2.5.2 was used locally.
python3 -m pip install -r tools/requirements-pcm-anchor-timing.txt
python3 tests/unit/apta_1_1_pcm_anchor_timing.py -v
python3 tests/unit/apta_1_1_multibeat_timing.py -v
python3 tests/unit/apta_1_1_event_representation.py -v
python3 tests/unit/apta_1_1_neural_reference.py -v
python3 tests/unit/apta_1_1_native_annotated_development.py -v
```

The NumPy-dependent suite is explicitly invoked; it does not introduce a new
dependency into the normal Rust/C CI or production runtime. Rust core, native
validators, frozen scoring tools, default feature paths and consumer code are
byte-identical. No new full Rust/C, sanitizer, device or hosted CI qualification
is claimed. Commit and push this preflight before the single clean-source
stage-A run. Synthetic behavior is not musical or physical acceptance.
