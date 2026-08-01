# APTA session seeding 0.1

**Status:** implementation candidate
**Entry point:** `apta_session_seed_from_result()`

## 1. Purpose

Partial serialization already worked in both the writer and the container: a
result whose overview is `PARTIAL` serializes, and the container flags it with
`APTA_CONTAINER_FLAG_PARTIAL_RESULT`. The other direction did not exist. A
partially analysed track had to be rescanned from zero.

`apta_session_seed_from_result()` initialises a fresh session's waveform
coverage from a parsed result, so analysis can continue where a checkpoint left
off.

## 2. Contract

```c
APTA_API apta_status_t APTA_CALL
apta_session_seed_from_result(
    apta_session_t *session,
    const apta_result_t *result);
```

Callable only in `APTA_SESSION_CREATED`, like `apta_session_set_source()`, and
subject to the same host-serialization rule as every other mutating call. It
takes the session's process lock and returns `APTA_ERROR_BUSY` if another
thread holds it.

| Return | Meaning |
|---|---|
| `APTA_STATUS_OK` | Coverage seeded. |
| `APTA_ERROR_INVALID_ARGUMENT` | `session` or `result` is NULL. |
| `APTA_ERROR_INVALID_STATE` | The session has left `APTA_SESSION_CREATED`. |
| `APTA_ERROR_BUSY` | Another call holds the session. |
| `APTA_ERROR_CONFLICT` | The result is incompatible; see below. |

The result is not retained. The caller may release it immediately afterwards.

## 3. What is seeded, and what is not

Seeded: the accepted-range table and the overview accumulators, for every valid
column the result carries. Those ranges are then treated as already analysed,
so pushing PCM for them is unnecessary and pushing it anyway conflicts as it
would for any other duplicate range.

Not seeded: tempo and beatgrid. A parsed result carries a published tempo and
grid but not the onset timeline they were derived from, and that timeline is
what the estimators actually consume. There is nothing to resume from, so the
engines rebuild their own evidence from the PCM that follows. Seeding waveform
coverage while letting the onset engines restart is the honest split; the
alternative would be presenting a cached tempo as though it had been
re-derived.

Columns are quantized in the container. The reconstruction inverts that
quantization -- `int16` peaks and a `uint16` RMS -- so a seeded column
reproduces the parsed column rather than the original accumulator state.
Expect agreement to within one count, not bit equality, on the seeded range.
Columns analysed after the seed are exact.

## 4. Compatibility checking is partial

This is the part a host has to read.

Checked by the library:

- the result carries `APTA_FEATURE_WAVEFORM_OVERVIEW`;
- `apta_waveform_level_info_t.frames_per_column` matches the session's overview
  resolution;
- no seeded range extends past the session's `total_frames`.

**Not checked, because the data does not exist:** the source sample rate, the
channel count and the source track length. `apta_result_info_t` carries none of
them and no container section records them. A result produced from a 48 kHz
stereo master and seeded into a 44.1 kHz mono session is accepted, and the
seeded columns then describe different audio than the session will analyse.

The caller must guarantee that the result came from the same source, decoded
the same way. In practice that means storing the checkpoint alongside whatever
identifies the source in the host's own library -- a track identifier and a
decoder configuration -- and refusing to seed when either differs. The session
metadata section is a reasonable place to carry that, since it round trips
through the container.

Closing this gap properly means recording source geometry in the container,
which is a format change requiring a section version bump and a
conformance-manifest update. That was outside the scope of the change that
added this entry point.

## 5. Example

```c
apta_session_t *session = NULL;
const apta_result_t *checkpoint = NULL;

apta_session_config_init(&config);
/* ... same source geometry as the checkpoint was produced with ... */
status = apta_session_create(context, &config, &session);

status = apta_result_parse(context, &parse_options, bytes, size, &checkpoint);
status = apta_session_seed_from_result(session, checkpoint);
apta_result_release(checkpoint);

/* Push only the frames the checkpoint did not cover. */
```

## 6. Verification

`apta.session.seeding` analyses half a track, serializes, creates a fresh
session, seeds it, analyses only the remainder, and compares the final overview
against an uninterrupted run of the same input: coverage is complete and
contiguous, columns after the seed match exactly, and seeded columns match to
within one count.

It also covers rejection of a NULL argument, of a mismatched column geometry,
and of a call made after the session has left `APTA_SESSION_CREATED`.
