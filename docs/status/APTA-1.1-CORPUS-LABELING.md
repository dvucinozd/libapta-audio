# APTA 1.1 corpus preparation and labeling

This document defines the local workflow for producing manually verified DJ
labels for the frozen APTA 1.1 acceptance corpus. It does not provide acceptance
evidence by itself.

## Why the acceptance corpus uses canonical WAV

The APTA core consumes PCM through the generic source API, so an application may
feed decoded MP3, FLAC or other formats through its own decoder. The current
reference desktop analyzer, however, opens WAV through
`apta_wav_decoder_open_path()`.

For qualification, frame-based truth must refer to the exact PCM stream being
analyzed. Therefore MP3/FLAC files are treated as **source material**, not as the
frame-coordinate authority. Each source is first converted to a canonical WAV;
that WAV is then labelled, SHA-256 frozen and analyzed.

The canonical corpus format is:

- RIFF/WAVE;
- 48,000 Hz;
- stereo;
- signed 16-bit little-endian PCM;
- source metadata removed by the preparation command.

This format also matches the principal 48 kHz embedded qualification profile.
The frozen hash is the canonical WAV hash, not the original MP3/FLAC hash.

## 1. Prepare MP3, FLAC and WAV sources

Install FFmpeg locally and place fresh source material under a private source
root. The preparation tool recursively accepts `.mp3`, `.flac` and `.wav`.

```sh
python3 tools/apta_1_1_prepare_corpus.py \
  --source-root /private/apta-raw \
  --output-root /private/apta-canonical \
  --manifest-output /private/apta-preparation.json
```

The tool invokes FFmpeg with a fixed output contract (`pcm_s16le`, 48 kHz,
stereo), validates every generated WAV, and records both source and canonical
SHA-256 values.

`apta-preparation.json` is **local-only** because it contains original source
paths. Do not commit it. The canonical WAV files are also private corpus data and
must not be committed.

If both `path/song.mp3` and `path/song.flac` exist, they would map to the same
`path/song.wav`; the tool rejects that collision rather than silently replacing
one track.

## 2. Open the local labeling helper

Open `tools/apta_1_1_label_corpus.html` in a modern browser. No web server or
network connection is required. The page contains no remote scripts and does
not upload audio.

Select the **canonical WAV**, not the MP3/FLAC source. The helper parses the WAV
header itself to obtain the exact source sample rate and frame count. Playback
is only used as a listening/scrubbing interface.

For canonical files in subdirectories, set the source field to the path relative
to the canonical corpus root, for example `house/set-a/track01.wav`.

## 3. Label musical key and meter

For each track record:

- tonic `0..11`, where `C=0`, `C#/Db=1`, ..., `B=11`;
- mode `major` or `minor`;
- meter `3/4` or `4/4`.

Use an independent manual reference procedure. Do not use the APTA candidate
output as ground truth.

## 4. Label downbeat phase

Locate a verified beat **1** of a bar and pause/scrub as closely as practical.
Use **Mark current as downbeat** (`M` shortcut). The helper converts playback
time to a source-frame coordinate using the WAV header sample rate.

The downbeat marker can be nudged by 1, 10 or 100 frames. The acceptance
protocol compares cyclic bar phase, so choose a clearly verified bar downbeat;
it does not have to be the very first audible transient in the file.

## 5. Measure beat period accurately

Tap BPM is provided for navigation, but the preferred final label uses two
manual beat markers separated by many beats:

1. mark the verified downbeat;
2. move to a later beat on the same stable grid;
3. mark it as the reference beat (`R` shortcut);
4. enter the exact number of beat intervals between markers, preferably 16–64;
5. choose **Derive BPM / beat period**.

The helper computes:

```text
beat_period_frames = (reference_frame - downbeat_frame) / beat_intervals
BPM = sample_rate * 60 / beat_period_frames
```

Using many intervals averages down manual cursor error and is preferable to a
single tap-tempo estimate.

## 6. Export the local staging CSV

Add each verified track and periodically download `staging_labels.csv`. The
schema exactly matches the corpus freezer:

```text
source,key_tonic,key_mode,meter_numerator,meter_denominator,downbeat_frame,beat_period_frames
```

The staging CSV is private because `source` may reveal filenames. The next step
removes that information.

## 7. Freeze only after review

After an independent review of all labels, freeze the canonical corpus:

```sh
python3 tools/apta_1_1_freeze_corpus.py \
  --corpus-root /private/apta-canonical \
  --staging-labels /private/staging_labels.csv \
  --labels-output qualification/labels.csv \
  --manifest-output qualification/manifest.json \
  --frozen-utc 2026-01-01T00:00:00Z \
  --reference-source manually-verified-fresh-corpus \
  --verification-procedure independent-dj-label-review
```

The freezer hashes the exact canonical WAV and replaces filenames with opaque
`track-<sha256-prefix>` IDs. From that point onward the corpus runner, result
exporter and acceptance evaluator operate on those frozen identities.

## MP3 versus FLAC implications

- **FLAC** is lossless compression. Decoding it to PCM does not intentionally
  discard musical information, but frame coordinates still belong to the
  canonical WAV used by the qualification run.
- **MP3** is lossy and decoder/container delay can make compressed-file timing a
  poor frame-coordinate reference. Always label the prepared WAV, never a time
  offset copied from an MP3 player.
- APTA itself is not intrinsically WAV-only. The reusable core analyzes PCM; the
  WAV restriction belongs to the current reference desktop file decoder and
  qualification CLI boundary.
