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

## Screen result

At clean instrument revision `fc7995944e55ed47a0878a4120a8373cf4bc13b4`,
installed Essentia `2.1-beta6-dev` with defaults and sampleRate=48000 matched
12/12 major and 12/12 minor stimuli. The installed snapshot confirms Hann,
4096 frame/hop, 60 peaks, 12-bin HPCP and bgate. No parameters were tuned.
The single fixed screen passes; no C port or corpus acceptance is claimed.

Full local report: `build/key-reference-screen/essentia.json`, SHA-256
`99e733250f604a0eed57317fb743ad7f02e576c940f0c0255bffe14719381a61`.
It includes all parameters, generated-PCM hashes, per-stimulus results and host
timings. The 24 extraction calls took about 0.068 seconds total on this host,
excluding imports/generation: a smoke timing, not a benchmark or P4 prediction.

Decision: retain Essentia as the sole external reference for one comparison on
already-open real-music development material. Verify that comparison before
implementing a bounded independent peak-based front end. No more local
Goertzel/profile variants or full native test reruns are justified by this screen.

## Fixed real-music comparison (before execution)

Use only the already-spent semitone-band FMAK 72-track development selection.
Reuse its canonical loader and verify the exact manifest, labels, private
mapping and all complete WAV hashes before extraction. Read PCM16/48kHz/stereo
without resampling; average channels in float. Keep the same KeyExtractor
parameters from the synthetic screen. No threshold/profile/window sweep.

Compare exact tonic AND mode to the frozen labels and retained default report.
Report total/per-mode matches, fixes, breaks and changed verdicts. Essentia
strength is not APTA confidence: confidence safety is unassessed, not passed.
An external reference merits port investigation only at >=70% total, >=60%
each mode and positive net fixes. Even a pass is NOT holdout eligibility or
native promotion. Stop on failed identity checks or execution errors; never
discard failed tracks or substitute a fresh corpus. Store opaque per-track
results locally; publish aggregates/hashes only. Native matrix rerun unnecessary.
