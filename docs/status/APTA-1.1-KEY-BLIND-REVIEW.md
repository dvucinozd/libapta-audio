# Key label provenance audit and blind review

This is a host-only diagnostic audit of the already-spent 72-track FMAK
development selection, not relabeling, algorithm tuning or acceptance.
The source metadata and all previous results remain immutable.

FMAK v1.0 describes expert song-level key/mode annotations, not automatic
Spotify labels. Spotify URIs are optional cross-references:
<https://zenodo.org/records/10719860>.

## Frozen procedure

`tools/apta_key_blind_review.py` reuses the existing metadata inventory,
selection and prepared-corpus validators. Require the pinned manifest and
baseline/reference hashes, exact original-source coverage, all 72 metadata
labels matching prepared/report labels, and complete canonical WAV hashes.
Stop on any mismatch; do not repair inputs. This does not independently
re-decode the source archive or establish the musical correctness of labels.

Select two tracks per original mode from each stratum: both algorithms wrong
and agreeing; both wrong and disagreeing; exactly one correct. Selection and
presentation order use separate fixed SHA-256 keys. Twelve intentionally
selected disagreements cannot estimate general accuracy or justify wholesale
corpus relabeling. Native DSP and holdouts are out of scope; no native rebuild.

Copy the complete canonical WAVs unchanged into a private `listener` directory
as A01 through A12, verify copy hashes, and provide a blank CSV with no default
tonic/mode. The static offline playlist contains no solutions, source IDs,
algorithm results or stratum assignments. Share only this directory with an
independent musician. The coordinator report is private and OUTSIDE it.
Allow abstention, unstable tonality and modulation notes. Preserve submitted
answers before unblinding; do not replace original labels or rescore them as
fresh acceptance. A second listener must work independently on a blank copy.

Expected cost: one sequential hash pass over 72 WAVs plus twelve local copies;
no extraction, model training, network upload or embedded resource change.
Verify host selection/mapping tests and the generated package only.

## Completed preparation

Executed at clean source `d7e8fb522b465b54b935d3a510ad51627965804f`:
72/72 original metadata labels match prepared labels and both retained reports;
72/72 complete canonical WAV hashes match the frozen private mapping. No
tonic/mode transfer mismatch was found. This is not an independent verification
of original source-to-audio assignment: the source archive was not re-decoded.

The private packet is `build/key-label-review/listener/`: twelve full WAVs,
an offline playlist, instructions and twelve blank CSV answer rows. All twelve
copy hashes and playlist links pass; the fifteen listener files contain no
private track IDs in their text. The hidden coordinator report is outside the
listener directory, SHA-256
`90d2b860fea3a694c15334356d1293731a8a0a9bc1506b228bdd7e4e8a349ed3`.
It records the source/tool/input hashes, selection and unchanged answer keys.

Scoped verification: `python -m unittest discover -s tools -p
test_apta_key_blind_review.py -v` (4/4), full identity-checked preparation,
blank-answer/link/privacy checks and `git diff --check`. No DSP flags, native
changes, extraction runs, holdout access, original-label edits or release gate
closures. Musical label truth is pending independent listening. Preserve that
review before comparing answers; a disagreement alone is not proof of bad labels.

## Listener form usability update

The listener index now uses `tools/apta_key_blind_review_form.html`: Croatian
dropdowns without default answers, notes, local browser draft recovery, and
plain-text download with a visible copy/paste fallback. CSV editing is optional.
Only the listener HTML/instructions changed; WAVs, mappings and original labels
remain unchanged. Browser smoke verified twelve cards, input, draft restoration,
and downloaded UTF-8 text (including the explicit Croatian H / English B label).
Test draft data was cleared. The earlier aggregate audit remains historical;
this UI update does not constitute another corpus evaluation.

## Source archive re-decode audit — 2026-09-04

The submitted listening answers were preserved unchanged before unblinding
(SHA-256 `4141e8b4f7a7b99ee461306158603cbd08b4ecdef1c12296eb048b03d54b6bb5`).
Agreement with that listener on this deliberately selected twelve-track sample:
original FMAK labels 3/12, native default 5/12, fixed Essentia reference 9/12.
These are agreements with one submitted review, not population accuracy or
independently adjudicated replacement ground truth.

`tools/apta_key_review_source_audit.py` then checked the complete 4,150,442,299-byte
source archive against the frozen MD5 and independently re-decoded the twelve
corresponding MP3 members with the original FFmpeg 6.1.1-3ubuntu5 and canonical
conversion helper. All twelve original CSV labels match the prepared/coordinator
labels by source ID. All twelve reconstructed full WAV hashes AND PCM hashes/
geometries match both the canonical corpus and the listener copies exactly.
No local source selection, label association or conversion discrepancy was
found for these twelve tracks. This does not establish that the upstream
dataset's musical annotations are correct or adjudicate listener disagreements.

Private machine-readable report:
`build/key-label-review/source-audit-20260904/report-private.json`, SHA-256
`8e4f62205707d13e2aa0b9e5fda72c8eb13d076e0c233a3dd966892492784add`.
Archive SHA-256:
`660aabdfc8d338a499bb635a99911051f98f2f532a0f4865da8c9f55257581e2`.
Host audit at HEAD `792f86c0b6a45c6a5d55560b402a3d886d6f766e` with uncommitted
form/audit tooling explicitly recorded (`source_worktree_clean: false`), not a
clean qualification build. Exact executed audit script SHA-256:
`f57b98d0fcb886d49afd19a0e35d61d23b088c31f6dde1c0ce9a6edb60c16ac8`.
Verification: Python syntax compilation, successful 12/12 identity-checked
re-decode and `git diff --check`. No native algorithms, labels, holdouts or
original checkout changed; no native test matrix or acceptance rerun.

Next boundary: independently adjudicate the nine listener/FMAK disagreements
before treating either label set as corrected truth. Keep both versions and
all original scoring reports; no automatic relabeling or port promotion.

## Independent second-listener packet

Prepared a separate blank nine-track packet from the fixed first-review/FMAK
disagreements, using `tools/apta_key_second_listener.ps1`. Renamed and reordered
full WAV copies use B01–B09; all nine copy hashes pass. The listener HTML has
its own draft namespace and TXT export prefix, and discloses no first-review
answers, original labels, algorithm results, mappings or selection rationale.
Instructions require independent listening without automated key detectors.
The private remapping stays outside both the listener folder and its ZIP.
Only share `build/key-label-review/listener-2.zip` (extract before opening).
All eleven ZIP entry contents match the listener files. Browser smoke verifies
nine blank cards, B01–B09 audio links, abstention/notes input and a downloaded
nine-row TXT. Test browser draft was cleared. First-review inputs are unchanged.
The subsequently preserved second-listener submission agrees with listener 1
on all nine items at high reported certainty; this still carries no acceptance
claim and is used only in the diagnostic service screen below.

## Free browser-service screen

After both listener submissions were frozen, two free local-browser analyzers
were run over the fixed twelve-track packet on 2026-09-04. Rotation and Zalturi
both state that audio remains in the browser. Rotation identifies its engine as
`essentia.js@2.1-beta6-dev`, so it is not independent of the existing Python
Essentia reference even though its multi-section/refinement workflow differs.
Zalturi does not document its key detector sufficiently to establish algorithmic
independence. VocalRemover was reachable in ordinary Chrome but its file chooser
could not be controlled by the automation environment; no audio was supplied
and no result was recorded. Cloudflare blocked its headless browser page.

Against listener 1 on all twelve: Rotation agrees 8/12 and Zalturi 4/12.
Against the two-listener consensus on the nine disputed tracks: original FMAK
0/9, native APTA 5/9, Rotation 6/9, fixed Python Essentia 7/9, Zalturi 3/9.
Zalturi returns major for 9/12 while the first listener returns major for 3/12,
so this screen does not support scaling it as a label authority. Rotation adds
no independent algorithm family and underperforms the already-recorded Essentia
reference on the consensus subset. Stop before running either service on the
remaining 60 tracks: automated estimates may triage review, but cannot replace
independent truth or close an acceptance gate.

Private report:
`build/key-label-review/web-service-screen/report-private.json`, SHA-256
`6d2c99fedc2ffe414db305607941543361dc3665f2ba3ecbee3722adbf83a74b`.
It retains service outputs, BPM/confidence fields, input hashes and per-sample
comparisons without source IDs. `tools/apta_key_web_service_screen.py` validates
12/12 service coverage, the immutable first/second submissions and their private
remapping before producing the comparison. Verification: successful complete
report generation, Python syntax compilation, zero private `track-*` IDs in the
report and `git diff --check`. No DSP execution, tuning, labels, holdout or
native code changed; this remains spent diagnostic evidence.

## OpenKeyScan and musical-key-finder triage — 2026-09-04

[OpenKeyScan](https://www.openkeyscan.com/api) desktop 1.0.0.0 was exercised
through its localhost REST API. Its published
[analyzer source](https://github.com/rekordcloud/openkeyscan-analyzer) describes
a CQT/CNN implementation, making this a more independent comparison than the
Essentia-based browser service. The fixed twelve-track screen agrees with
listener 1 on 11/12 and with the two-listener consensus on 8/9. The sole miss in
the consensus subset is A01: OpenKeyScan returns F-sharp minor and both listeners
return A major. This small, deliberately selected screen supports using the tool
for automated triage only; it does not estimate population accuracy.

The suggested
[jackmcarthur/musical-key-finder](https://github.com/jackmcarthur/musical-key-finder)
was also run at frozen commit `34775ac56df3833fe3d5a21d7ffd1dfbf1d4a460`,
using its `Tonal_Fragment` class, Librosa 0.11.0 and the README-recommended HPSS
harmonic input. After enharmonic normalization it agrees with listener 1 on
5/12 and with the two-listener consensus on 4/9, so stop after the targeted
screen. The pinned checkout contains no LICENSE file; do not copy or port its
implementation into LIBAPTA.

OpenKeyScan was then run once over all 72 already-spent development tracks.
Agreement counts are comparisons, not accuracy, because the FMAK labels are in
dispute: OpenKeyScan/FMAK 33/72, OpenKeyScan/native APTA 25/72 and
OpenKeyScan/fixed Essentia 50/72. Automatic agreement topology is: all three
algorithms 18, OpenKeyScan+Essentia only 32, OpenKeyScan+APTA only 7,
APTA+Essentia only 0, all different 15. Therefore the smallest useful optional
follow-up set is the 22 OpenKeyScan/Essentia disagreements, not the remaining
sixty tracks. Do not automatically relabel even the 50 agreements and do not
use this spent set to close a release gate.

`tools/apta_key_openkeyscan_development.py` copies one WAV at a time to a unique
temporary path because OpenKeyScan may write tags according to application
settings. All 72 canonical full-file hashes remained unchanged; all 72 tagged
copies changed full-file hash but retained exactly identical PCM geometry and
SHA-256, then were deleted. Analyzer identity: desktop executable SHA-256
`236703eae114cf70b3f4b54b53458cb177cbd2db13b892b58dcf822e19db3aed`,
analyzer executable SHA-256
`4e3cfcf034ecc9dc494b960413f40844f6a7726ed16406018b287846172efea9`.

Private reports:

- `build/key-label-review/openkeyscan-development/report-private.json`, SHA-256
  `d1b45badaa03639303a3fa1a3bc6c42f60304fca71c4dd404d7a3edd7eaae66c`;
- `build/key-label-review/musical-key-finder-screen/report-private.json`, SHA-256
  `b7c64dc747baecb66f13b4aa94f4ec75eefe9e0d018719cf3669caa7b5987018`;
- `build/key-label-review/automated-triage-screen/report-private.json`, SHA-256
  `34ce29a46de709d54dad6c6f73e52c64bba183c7cf36215911676d51cf56a3fe`.

Verification: frozen manifest/baseline/Essentia and listener hashes, complete
72/72 API coverage, complete PCM/source preservation, exact 12/12 and 9/9
review coverage, Python syntax compilation and `git diff --check`. No native DSP,
corpus labels, holdout, acceptance threshold or release state changed.

## Frozen 22-case topology — 2026-09-05

The report-only procedure in
[`APTA-1.1-KEY-DISAGREEMENT-TOPOLOGY.md`](APTA-1.1-KEY-DISAGREEMENT-TOPOLOGY.md)
was recorded before execution. Five pinned reports passed full SHA-256 checks,
72/72 unique identity and retained-verdict checks, and the twelve-review /
nine-consensus mapping checks. No audio or detector was run. Source baseline
is `d60e0cd61dacf48d5f251a1b0c403dff443fbeb9`; the diagnostic tool and documents
were uncommitted and the unrelated local `output/` remained present, so this
is explicitly dirty-worktree diagnostic evidence, not clean qualification.

| OpenKeyScan / Essentia relationship | Cases / 22 |
|---|---:|
| Same tonic, opposite mode | 8 |
| Same mode, fourth/fifth tonic relationship | 6 |
| Relative major/minor | 3 |
| Other tonic and mode differences | 5 |
| Other same-mode tonic differences | 0 |

Sixteen disagreements are OpenKeyScan minor / Essentia major; four are both
minor and two both major. All eight same-tonic mode disagreements point from
OpenKeyScan minor to Essentia major. Relative-key ambiguity is therefore a
minority of this fixed subset, not a general explanation of its disagreements.
These relationships do not identify the correct detector or prove modulation.

Native APTA selects minor on all 22 disagreements and 48/50 external agreements:
70/72 minor overall. In contrast, OpenKeyScan returns 51/72 minor and Essentia
35/72. Even where both external tools agree on major (19 tracks), native APTA
returns minor on 18, including 12 with the very same tonic. This is a repeated
native mode asymmetry outside the deliberately disputed subset as well. It
supports investigating representation/score contrast; automated agreement
alone does not establish native error or replacement ground truth.

Only three of the 22 cases were in listener 1's packet and only two have both
listeners. On those two, native APTA and OpenKeyScan each agree on one, Essentia
on neither, and one is missed by all three algorithms. The remaining twenty
have no two-listener consensus in the retained packet. Conversely, seven of
the 50 external agreements have both listeners, and all seven agree with both
external tools. These intentionally selected counts cannot justify blanket
relabeling, general accuracy, or choosing an automatic tie-breaker.

### Decision and next boundary

The 22-case topology task is complete. Retain the mode/tonal-contrast hypothesis
for a separately frozen trace diagnostic, not a detector override. Prior
synthetic evidence already reproduces major-to-minor collapse and the double
extraction reference does not fix it; this report adds real-song verdict
topology but no acoustic evidence establishing the cause.

The existing `tools/apta_key_trace.c` writes final accumulated spectral/chroma
vectors only. The specific missing observation is per-window energy before
`log1p`, after compression and octave folding, and after cumulative addition
in `apta_key_finish_window`, together with unquantized major/minor score
margins. A proposed follow-up should stream these observations to host-only
diagnostics with bounded scratch, preserve production bytes and every default
decision, and first verify them on the fixed synthetic progressions. It should
measure where contrast changes, without subtracting a fitted floor, rescoring
the corpus, copying an external detector, or choosing thresholds from disputed
labels. Only after a causal diagnostic supports one representation change
should that change receive its own preregistration and independent development
evidence. This step implements none of that follow-up and opens no holdout.

### Reproduction and evidence

From the DSP worktree at the recorded baseline, with the new host tool present:

```powershell
python -m unittest discover -s tools -p test_apta_key_disagreement_topology.py -v
python tools/apta_key_disagreement_topology.py --output build/key-disagreement-topology-20260905/new-private.json
```

The output must be new and under ignored `build/`. Standard output contains
only the public aggregate report. The private output includes opaque per-track
joins and must stay local. The tool intentionally requires the frozen baseline
HEAD; moving the source requires an explicit protocol/provenance update.

All 10 host tests pass, including exhaustive symmetry/transposition checks of
all 576 key pairs, invalid input and review-mapping rejection, and output
overwrite protection. The replay is byte-identical. Private report:
`build/key-disagreement-topology-20260905/report-private.json`, SHA-256
`c5fcc12ff5abccc287234d028a6b9a880c0af49823c086c8f24c000e4ebf0ff2`.
The public aggregate is
[`../../evidence/1.1/key-disagreement-topology-20260905.json`](../../evidence/1.1/key-disagreement-topology-20260905.json),
with exact input/tool/test/protocol hashes. Native flags, CPU/RAM, APIs, wire
format, labels and release state are unchanged. Fixes/breaks and confidence
safety are unassessed because this is neither a candidate comparison nor an
adjudicated accuracy run. No native rebuild or P4 qualification was performed.
