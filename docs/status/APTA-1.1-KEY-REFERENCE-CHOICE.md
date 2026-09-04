# Key implementation choice — 2026-09-04

Choose one existing implementation before another native experiment: Essentia
KeyExtractor with its installed defaults, overriding only sampleRate=48000.
Record version and every parameter. Run once on 24 clean four-second I-IV-V-I /
i-iv-V-i synthetic progressions, one of each tonic/mode. Same mathematical
stimuli as the previous diagnostic; NumPy generation is not claimed to be
sample-bit-identical to the C generator. No parameter sweep, noise variants,
corpus access or acceptance claim. Require >=9/12 matches in each mode to
justify a subsequent real-music development comparison; otherwise stop this
reference screen. This smoke threshold is not a release/development gate.

## Verified primary sources

- [Essentia KeyExtractor](https://essentia.upf.edu/reference/std_KeyExtractor.html):
  windowed spectral analysis, HPCP and key estimation; documented frame/hop
  4096, peak cap 60, profile bgate. Use actual installed parameter snapshot.
- [HPCP](https://essentia.upf.edu/reference/std_HPCP.html): input is detected
  spectral peak frequencies/magnitudes, not just 36 preselected resonators.
- [libkeyfinder](https://github.com/mixxxdj/libkeyfinder): deployed in Mixxx,
  C++11, FFTW dependency, GPL-3.0-or-later. Useful alternative reference, but
  do not start a second implementation/build during this bounded screen.
- [Essentia licensing](https://essentia.upf.edu/licensing_information.html):
  AGPL/commercial licensing is documented. Use installed library as a separate
  local reference; do not copy/link its source into LIBAPTA's Apache-2.0 core.
  Any future distribution/integration requires a license review.

## P4 feasibility, not qualification

A separately written streaming implementation could keep bounded buffers:
4096 float input samples (16 KiB), complex FFT (32 KiB), magnitude (about 8 KiB),
window (16 KiB), twiddles (about 16 KiB), 60 peak pairs and a small HPCP vector.
This preliminary buffer estimate is under 96 KiB, excluding FFT-backend scratch,
stack and integration overhead; it is NOT measured total memory or approval
to alter frozen resource limits. 48 kHz / 4096 hop gives about 11.72 frames/s.
Real P4 timing and memory remain unmeasured. The complete Essentia runtime is
not proposed for the embedded target.

If the reference passes this screen, compare the unchanged reference on an
already-open development corpus before allocating effort to a bounded C port.
No untouched holdout is authorized by a synthetic pass. No full native matrix
is needed for this host-only screen.
