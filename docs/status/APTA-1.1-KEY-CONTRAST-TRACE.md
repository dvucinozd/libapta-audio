# Synthetic key contrast trace — result 2026-09-05

The frozen [protocol](APTA-1.1-KEY-CONTRAST-TRACE-PROTOCOL.md) was written before
implementation. Baseline HEAD is `d60e0cd61dacf48d5f251a1b0c403dff443fbeb9`;
the observer, summary and preceding topology work remain uncommitted. This is
synthetic diagnostic evidence from a dirty worktree, not qualification.

## Outcome

The first observed major-mode loss is already present after compression and
folding of the first tonic-chord window. With the same frozen cosine scorer,
raw folded energies identify all twelve clean major tonic chords in both
builds; compressed window vectors identify only seven in default and two in
semitone-band. These raw scores are counterfactual observations, not a linear
frontend proposal or corpus accuracy result.

| Clean major stimuli, each across 12 transpositions | Default | Semitone-band |
|---|---:|---:|
| First tonic window, raw-folded diagnostic matches | 12/12 | 12/12 |
| First tonic window, compressed native matches | 7/12 | 2/12 |
| Final tonic window alone, compressed diagnostic matches | 9/12 | 1/12 |
| Four-window native cumulative matches | 4/12 | 0/12 |
| First raw-folded min/mean chroma | 0.0001 | 0.0008 |
| First compressed-window min/mean chroma | 0.4309 | 0.6011 |
| Final cumulative min/mean chroma | 0.5722 | 0.7567 |

The independently calculated best-major minus best-minor cosine margin on the
first major window changes from +0.03943 to +0.00025 in default and from
+0.03921 to -0.01119 in band. After accumulation it is -0.00745 and -0.01422,
respectively. These are means over transpositions, not margins for every item.
On clean minor progressions, final native cumulative matches remain 11/12 and
12/12. A blanket mode inversion would therefore be unsupported.

The octave-resolved evidence also changes before folding: normalized entropy
on the first major window rises from 0.3089 to 0.8544 in default and from
0.3139 to 0.8904 in band after compression (band averages its three probes).
The raw-folded versus compressed-folded comparison holds the folding and
scorer fixed. It directly exposes the effect of the current compression on
these measured synthetic energies. Accumulation adds further flattening and
mode loss on these progressions. This does not establish the cause of every
real-song error, recommend returning to linear energy, or assess DJ accuracy.

## Instrument and verification

The explicit `apta_key_contrast_diagnostic` target compiles its own native key
translation unit with `APTA_INTERNAL_KEY_CONTRAST_DIAGNOSTIC=1`. Two guarded
observation sites report the actual sanitized resonator energy and actual
compressed float value. The production library never receives this macro.
The existing synthetic generator, profiles, native score checks and decisions
are unchanged. The observer uses bounded test-process scratch, not session
state: 476 bytes for default, 1,124 bytes for band. Session sizes remain 11,824
and 12,784 bytes; the defining core header is byte-identical to baseline.

Verified:

- Release/Werror builds pass for default and band.
- Every one of 288 observed windows per build reconstructs native cumulative
  chroma bit-for-bit in original addition order.
- Removing the added trace fields yields all 720 original diagnostic rows
  exactly, against the previously frozen default/band report hashes.
- Repeated default and band reports are byte-identical.
- Band ASan/UBSan completes without diagnostics and produces the same report
  bytes as Release.
- Eight summary tests pass, including all 24 ideal-profile identities,
  malformed/empty evidence, compression/folding corruption, incomplete
  observation and changed-baseline rejection.
- Both production analyzer binaries and both native key object files are
  byte-identical to their pre-change hashes. The observer symbol is absent
  from the default production analyzer.

No full native matrix was rerun: unchanged production objects/analyzers and
the exact 720-row comparison preserve the established baseline, while the new
instrument received its focused Werror/sanitizer checks. No audio corpus,
labels, holdout, native candidate, confidence calibration or P4 run was used.

## Reproduction and retained evidence

Configure the usual static Release/Werror test build with all key experiments
off for default, and with only
`APTA_ENABLE_EXPERIMENTAL_SEMITONE_BAND_KEY=ON` for band. Explicitly build
`apta_key_contrast_diagnostic`, then run it with `--json`. A band Debug build
with `APTA_ENABLE_SANITIZERS=ON` supplies sanitizer coverage. Store outputs in
a new ignored directory; never overwrite retained reports.

```powershell
python -m unittest discover -s tools -p test_apta_key_contrast_summary.py -v
python tools/apta_key_contrast_summary.py --default build/key-contrast-20260905/default.json --band build/key-contrast-20260905/band.json --baseline-default build/key-mode-diagnostic/default.json --baseline-band build/key-mode-diagnostic/band.json --output build/key-contrast-20260905/new-summary.json
```

Full traces, repeats, build logs and all-condition summaries are local under
`build/key-contrast-20260905/`. The summary SHA-256 is
`1d4c05b9f15e7facb653e214fe2924c3f4ad5b5bf2f71f35b991125b1efd49cd`.
The public [evidence snapshot](../../evidence/1.1/key-contrast-trace-20260905.json)
records source/instrument/report hashes, verification, resource limits and
clean first/final-window aggregates. The complete summary also retains the
frozen detuned/noisy conditions and intermediate windows without labelling
individual IV/V chords as global-key errors.

## Next boundary

The contrast observation task is complete. Before choosing a replacement
compression rule, freeze a bounded gain-sensitivity diagnostic: vary only
input gain on the same synthetic progressions, preserving keys, durations,
profiles and windows, and test whether absolute-energy `log1p` changes mode
decisions as signal level changes. This can distinguish an input-level effect
from a need for different temporal evidence and inform one separately frozen
energy-normalization/contrast hypothesis. Do not sweep compression parameters,
subtract fitted floors or revive rejected centered correlation on these
results. A new detector still requires independent development transfer and
confidence/resource gates; formal holdouts and final acceptance remain closed.
