# HS-03 bounded streaming resampler result

Date: 2026-10-26. Verdict: **HS03_SYNTHETIC_HOST_PASS_ONLY**.
Clean implementation commit: `837b6acd5d366cb00627053a104e3762b09316e1`.
[Protocol](PRODUCT-HOST-RESAMPLER-PROTOCOL.md), [implementation](PRODUCT-HOST-RESAMPLER-IMPLEMENTATION.md),
[machine receipt](PRODUCT-HOST-RESAMPLER-RESULT.json).

All six registered cases ran once in fixed order, with 107 validated observations:
102 short artificial cases, four 600s streams and one asynchronous cancel. Every
completed output/input hash, exact count, phase availability, work/MAC count and
status matches the frozen scalar oracle. No source audio, weights, real logits,
labels or formal holdout was read. This is host resampler evidence only.

## Host measurements

| Case | Source frames | Output samples | Driver wall s | Child CPU s | Child peak RSS KiB |
|---|---:|---:|---:|---:|---:|
| full44100-mono | 26460000 | 13230000 | 3.880181 | 3.797569 | 18832 |
| full44100-stereo | 26460000 | 13230000 | 4.229068 | 4.177227 | 18708 |
| full48000-mono | 28800000 | 13230000 | 3.914115 | 3.859533 | 18812 |
| full48000-stereo | 28800000 | 13230000 | 4.115554 | 4.073343 | 18652 |

Each full case performs 859950000 FIR MACs; units are 912870000 at 44100 Hz and
915210000 at 48000 Hz. Driver timings include synthetic input generation, PCM
validation/mixing, FIR scheduling, output/input SHA updates and flow bookkeeping.
They do not isolate only the FIR kernel and exclude frontend/neural/audio-codec
work. Worker and supervisor wall timings, including independent scalar/hash checks,
are separately recorded in JSON. Largest observed 4096-unit Driver.step call:
**4.337125ms**. These are single host observations, not worst-case or P4 limits.

Output counts are exactly 13230000 for every 600s source; no extra FIR tail or
timestamp delay is added. First output becomes available after 33 source frames
for these long cases. f32 hashes match bit-for-bit under the fixed separate
F64 multiply/add order, with full 65-tap zero-padding normalization at boundaries.

Storage is bounded independently of duration: ring **260 B**, static F64 tables
**76960 B**, input buffer 4096 B and output buffer 1024 B. On this host, Session
value size is 368 B and Driver value size 5856 B, including fixed buffers and SHA
controls. These are value/buffer sizes, not stack high-water, whole-device peak,
RAM placement or approved PSRAM budgets. Neither full input nor full output is
retained. Child rusage includes conservative pre-exec fork high-water; sampled
per-process/process-group envelopes are retained independently.

Asynchronous request after 10ms was serviced in **0.002790ms**, observed in
phase **Convolve**. At cancellation there were 75226 consumed frames and
34542 provisional outputs. Their prefix hashes are independently checked.
No final certificate was exposed and clearing the request did not resume work.
Separately observed lexical scope end: 0.000133ms; Session/Driver have no heap
allocation to reclaim. That interval is not a device cleanup/memory-reclamation
qualification. Host cancel does not qualify the full P4 product 50ms target.

## Numerical and operational integrity

All 102 short cases pass 6120 input/output/quota schedules per debug and release
profile. Counts, hashes and source-position rational arithmetic are invariant.
Tests cover canaries/mid-tap privacy, backpressure, source gaps/repeats/oversupply,
malformed stereo, unsupported profiles, invalid PCM in both channels and after
provisional output, explicit short EOF and cancellation in all three phases.

The frozen direct scalar oracle uses the original CS-02 tables and ascending
F64 operations; its fsum comparison stays within the unchanged CS-02 tolerance.
Full periodic interior compression matches the direct 4097-frame reference at
both rates/channels before candidate implementation. Every full measured digest
matches those pre-registered golden values. No filter/tolerance/cap was tuned.

Short fixture output extrema were -1.1351769 to 1.1351769. These demonstrate that
FIR ringing can exceed normalized input amplitude. No output clipping is applied.
The next frontend registration must bound its actual windowed input envelope;
the existing FFT oracle contract for PCM in[-1,1] must not be silently claimed to
cover larger FIR outputs. This does not change resampler results or CS-02 assets.

Caps remained CPU 60s, wall 90s, per-process RSS 64MiB, AS 128MiB and aggregate 1MiB.
All measured cases stayed inside. Fixed wall/RSS/output/AS violation probes were
refused; the allocating RSS child reached 46884KiB against 16MiB. The audit binds
165 parent files, actual Rust/Cargo binaries/features, loaded Python source/cache/
interpreter/shared libraries, ELF dependencies, exact source/protocol/fixture/reference/
coefficient/executable bytes and child stdout/stderr. 58 retained output files total
406115B including final aggregation. No partial attempt was dropped or replaced.
This is resource supervision and API/input isolation, not an OS filesystem sandbox.

Executable SHA-256: `7fac956c0b8a072be2c7173b1094baaba6875e743b0d45584b2125dfc2b4d1d2`.
Rust/Cargo 1.95.0, Python 3.12.3, release opt-in build on WSL; exact versions/digests
are in the machine receipt and were bound before execution. External evidence:
`D:/AI/LIBAPTA/outputs/hs03-measured-20261026/`; manifest audit:
`D:/AI/LIBAPTA/outputs/hs03-measured-20261026-audit.json`.

## Executed checks and remaining gates

Six new tests pass in debug/release, zero ignored. Existing waveform controls:
6 pass/3 ignored; waveform_result: 5 pass/1 ignored; default context: 5 pass/1 ignored.
All five existing C-oracle-dependent checks are **NOT_RUN**, not PASS. fmt, focused
Clippy(-D warnings), release CLI build and Python compile pass. No unrelated C/P4
matrix ran. Defaults/core/consumer/wire/HS-01/HS-02 and VERSION 1.0.1 are unchanged.

HS-03 closes this synthetic streaming resampler task only. Next software step is
a separately registered centered Hann/FFT1764/filter/log/difference frontend,
including its actual FIR-output amplitude envelope and independent analytic/DFT
oracles. No frontend/FFT/neural model ran here. Neural rights and checkpoint
training independence remain unresolved; PF-01 still requires the unavailable
JC1060P470C_I_W_Y and observed silicon identity. Full native product integration,
audio codecs/progress/publication, musical transfer/acceptance and physical timing/
memory qualification remain NOT_RUN. No provider/release is activated and no old
rejected evidence is reclassified. Product PF-04 stays NOT_RUN; H1 stays deferred.
