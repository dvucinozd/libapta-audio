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
