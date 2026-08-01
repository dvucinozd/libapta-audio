// SPDX-License-Identifier: Apache-2.0
/*
 * Tempo accuracy corpus and harness.
 *
 * B1-B3 cannot be evaluated on impulse trains, and copyrighted audio cannot be
 * committed, so this synthesizes a repeatable corpus of multi-layer drum
 * patterns -- kick, snare, hat and a bassline -- at known tempi, runs each to
 * END_OF_INPUT, and reports the selected tempo against ground truth.
 *
 * Everything here is deterministic: the noise source is a seeded xorshift, so
 * two runs of the same binary produce identical audio and identical results.
 *
 * IMPORTANT: results from this tool describe SYNTHETIC material. Synthetic
 * drum patterns have exact timing, no swing beyond what is programmed, no
 * expressive dynamics and no production processing. They are a floor, not a
 * prediction of real-world accuracy. Any reported rate must say so.
 *
 * Build:
 *   cc -O2 -std=c11 -Iinclude tools/apta_tempo_corpus.c build/libapta.a -lm \
 *      -o build/apta-tempo-corpus
 *
 * Usage:
 *   apta-tempo-corpus [--seconds N] [--write-wav DIR] [--verbose]
 *   apta-tempo-corpus --tracks LIST [--verbose]
 *
 * --tracks reads a plain-text list of real recordings with known ground-truth
 * tempo, one per line:
 *
 *     # comments and blank lines are ignored
 *     /music/track-a.wav   128
 *     /music/track-b.wav   174.5
 *
 * Paths are used as given; nothing is copied into the repository, so no
 * copyrighted audio is committed. Results from a real corpus are labelled as
 * such, because the two kinds are not comparable: synthetic material has exact
 * timing and no production processing, and its rates are a floor.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>
#include <apta/desktop/apta_decoder.h>

#define SAMPLE_RATE 44100u
#define DEFAULT_SECONDS 30u
#define BLOCK_FRAMES 1024u

/*
 * Confidence at or above this is "the host would act on it": Sync and Quantize
 * are gated on confidence, and a confidently wrong grid is worse than an
 * absent one. B1's acceptance asks for the threshold to be stated explicitly.
 *
 * 75 is not assumed, it is read off the sweep printed below: it is the lowest
 * gate at which no incorrect answer survives while a useful number of correct
 * ones do. Anything lower admits octave errors.
 */
#define ACTIONABLE_CONFIDENCE 75u

/* ------------------------------------------------------------------ */
/* deterministic noise                                                  */

static uint32_t g_rng = 0x13579bdfu;

static void rng_seed(uint32_t seed)
{
    g_rng = seed != 0u ? seed : 0x13579bdfu;
}

static float rng_bipolar(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return (float)((double)g_rng / 2147483648.0 - 1.0);
}

/* ------------------------------------------------------------------ */
/* voices                                                               */

/* Kick: pitch-swept low sine with a fast body decay and a click transient. */
static void add_kick(float *out, size_t total, size_t at, float gain)
{
    const double decay = 0.16;
    const size_t length = (size_t)(decay * 4.0 * SAMPLE_RATE);
    size_t i;

    for (i = 0u; i < length && at + i < total; ++i) {
        const double t = (double)i / SAMPLE_RATE;
        const double env = exp(-t / decay);
        /* 110 Hz falling to 45 Hz over the first 40 ms. */
        const double f = 45.0 + 65.0 * exp(-t / 0.04);
        const double body = sin(6.283185307 * f * t);
        const double click = t < 0.004 ? (1.0 - t / 0.004) * 0.5 : 0.0;
        out[at + i] += (float)(gain * env * (body * 0.9 + click));
    }
}

/* Snare: noise band plus a 190 Hz shell tone. */
static void add_snare(float *out, size_t total, size_t at, float gain)
{
    const double decay = 0.11;
    const size_t length = (size_t)(decay * 4.0 * SAMPLE_RATE);
    float previous = 0.0f;
    size_t i;

    for (i = 0u; i < length && at + i < total; ++i) {
        const double t = (double)i / SAMPLE_RATE;
        const double env = exp(-t / decay);
        const float raw = rng_bipolar();
        /* One-pole high-pass so the noise sits above the kick. */
        const float hp = raw - previous * 0.85f;
        const double shell = sin(6.283185307 * 190.0 * t) * exp(-t / 0.05);
        previous = raw;
        out[at + i] += (float)(gain * env * (hp * 0.7 + shell * 0.45));
    }
}

/* Hat: very short differenced noise, closed or open. */
static void add_hat(float *out, size_t total, size_t at, float gain, int open)
{
    const double decay = open ? 0.12 : 0.028;
    const size_t length = (size_t)(decay * 4.0 * SAMPLE_RATE);
    float previous = 0.0f;
    size_t i;

    for (i = 0u; i < length && at + i < total; ++i) {
        const double t = (double)i / SAMPLE_RATE;
        const double env = exp(-t / decay);
        const float raw = rng_bipolar();
        const float hp = raw - previous;
        previous = raw;
        out[at + i] += (float)(gain * env * hp * 0.5);
    }
}

/* Bass: plucked sine with a short attack, following a note pattern. */
static void add_bass(float *out, size_t total, size_t at, float gain,
                     double frequency, double seconds)
{
    const size_t length = (size_t)(seconds * SAMPLE_RATE);
    size_t i;

    for (i = 0u; i < length && at + i < total; ++i) {
        const double t = (double)i / SAMPLE_RATE;
        const double attack = t < 0.006 ? t / 0.006 : 1.0;
        const double release = exp(-t / (seconds * 0.55));
        const double tone = sin(6.283185307 * frequency * t) * 0.8 +
                            sin(6.283185307 * frequency * 2.0 * t) * 0.2;
        out[at + i] += (float)(gain * attack * release * tone);
    }
}

/* ------------------------------------------------------------------ */
/* patterns                                                             */

typedef struct {
    const char *name;
    /* 16 sixteenth-note steps per bar. 1 = hit. */
    uint8_t kick[16];
    uint8_t snare[16];
    uint8_t hat[16];
    uint8_t open_hat[16];
    /* Bass note index per step, -1 for silence. Indexes semitone_offsets. */
    int8_t bass[16];
    const char *description;
} pattern_t;

static const int semitone_offsets[4] = {0, 0, 3, 5};

static const pattern_t g_patterns[] = {
    {
        "four_on_floor",
        {1,0,0,0, 1,0,0,0, 1,0,0,0, 1,0,0,0},
        {0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0},
        {0,0,1,0, 0,0,1,0, 0,0,1,0, 0,0,1,0},
        {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,1,0},
        {0,-1,-1,-1, 0,-1,-1,-1, 2,-1,-1,-1, 3,-1,-1,-1},
        "house/techno, kick on every beat"
    },
    {
        "breakbeat",
        {1,0,0,0, 0,0,1,0, 0,0,1,0, 0,0,0,0},
        {0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,1},
        {1,0,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,0},
        {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0},
        {0,-1,-1,-1, -1,-1,2,-1, -1,-1,-1,-1, 3,-1,-1,-1},
        "syncopated kick, backbeat snare"
    },
    {
        "halftime",
        {1,0,0,0, 0,0,0,0, 0,0,1,0, 0,0,0,0},
        {0,0,0,0, 0,0,0,0, 1,0,0,0, 0,0,0,0},
        {0,0,1,0, 0,0,1,0, 0,0,1,0, 0,0,1,0},
        {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0},
        {0,-1,-1,-1, -1,-1,-1,-1, 2,-1,-1,-1, -1,-1,-1,-1},
        "snare on 3, sparse -- the classic half/double trap"
    },
    /*
     * The four patterns above place almost nothing on odd sixteenths, which
     * made the swing knob a no-op: it delays odd steps, and there was nothing
     * there to delay. These two carry sixteenth-note hats and ghost snares, so
     * swing actually engages. They also broaden the corpus toward material
     * where the subdivision is audible rather than implied.
     */
    {
        "sixteenth_hats",
        {1,0,0,0, 0,0,1,0, 1,0,0,0, 0,0,1,0},
        {0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0},
        {1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1},
        {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0},
        {0,-1,-1,-1, -1,-1,2,-1, 0,-1,-1,-1, 3,-1,-1,-1},
        "driving sixteenth hats over a syncopated kick"
    },
    {
        "ghost_funk",
        {1,0,0,1, 0,0,1,0, 0,0,1,0, 0,1,0,0},
        {0,0,1,0, 1,0,0,1, 0,1,0,0, 1,0,1,0},
        {1,0,1,1, 1,0,1,1, 1,0,1,1, 1,0,1,1},
        {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0},
        {0,-1,-1,2, -1,-1,-1,-1, 3,-1,-1,-1, -1,-1,2,-1},
        "ghost snares on the e and the a, the classic shuffle carrier"
    },
    {
        "offbeat_bass",
        {1,0,0,0, 1,0,0,0, 1,0,0,0, 1,0,0,0},
        {0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0},
        {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0},
        {0,0,1,0, 0,0,1,0, 0,0,1,0, 0,0,1,0},
        {-1,-1,0,-1, -1,-1,0,-1, -1,-1,2,-1, -1,-1,3,-1},
        "offbeat open hats and bass, strong eighth-note energy"
    }
};

#define PATTERN_COUNT (sizeof(g_patterns) / sizeof(g_patterns[0]))

static const uint32_t g_tempos[] = {
    90u, 100u, 110u, 118u, 124u, 128u, 132u, 140u, 150u, 174u
};

#define TEMPO_COUNT (sizeof(g_tempos) / sizeof(g_tempos[0]))

/*
 * Realism knobs.
 *
 * Programmed patterns with exact timing are an optimistic floor: every onset
 * lands on a grid line, every hit is the same loudness, and the tempo never
 * moves. Real material does none of that, and an autocorrelation estimator is
 * exactly the kind of thing those imperfections can degrade.
 *
 * These are off by default so the original baseline stays reproducible, and on
 * with --humanize. Values are chosen to be modest rather than adversarial: a
 * competent player or a lightly quantized production, not a sloppy one.
 */
static int g_jitter;
static int g_dynamics;
static int g_drift;
static int g_swing;

/* Standard deviation of per-hit timing error, seconds. Around 6 ms is typical
 * of a tight human performer; fully quantized electronic music has none. */
#define HUMAN_JITTER_SECONDS 0.006

/* Per-hit gain varies by up to this fraction, on top of a downbeat accent. */
#define HUMAN_VELOCITY_RANGE 0.25

/* Peak tempo deviation. One full sine cycle across the track, so the mean
 * tempo stays at nominal and the ground truth remains honest. */
#define HUMAN_DRIFT_DEPTH 0.005

/* Fraction of a 16th that odd steps are pushed late. 0.15 is a light shuffle;
 * it moves subdivisions without changing the beat period. */
#define HUMAN_SWING 0.15

/* Approximately gaussian, from the same deterministic source. */
static double human_jitter(void)
{
    if (!g_jitter) {
        return 0.0;
    }
    return (double)(rng_bipolar() + rng_bipolar() + rng_bipolar()) / 3.0 *
           HUMAN_JITTER_SECONDS * 3.0;
}

static float human_gain(float base, uint32_t step)
{
    float accent;

    if (!g_dynamics) {
        return base;
    }
    /* Downbeat loudest, backbeats next, offbeats quietest. */
    accent = (step % 16u) == 0u ? 1.12f
           : (step % 4u) == 0u  ? 1.04f
                                : 0.92f;
    return base * accent * (1.0f + rng_bipolar() * HUMAN_VELOCITY_RANGE);
}

static void render(const pattern_t *pattern,
                   uint32_t bpm,
                   float *out,
                   size_t total)
{
    const double nominal_step = 60.0 / (double)bpm / 4.0;  /* 16th note */
    const double track_seconds = (double)total / SAMPLE_RATE;
    size_t step_index = 0u;
    double cursor = 0.0;  /* seconds, start of the current 16th */

    rng_seed(0x2468aceu ^ bpm);
    memset(out, 0, total * sizeof(*out));

    while (cursor < track_seconds) {
        const uint32_t s = (uint32_t)(step_index % 16u);
        /* One sine cycle across the track keeps the mean at nominal. */
        const double drift =
            g_drift
                ? 1.0 + HUMAN_DRIFT_DEPTH *
                            sin(6.283185307 * cursor / track_seconds)
                : 1.0;
        const double step_seconds = nominal_step / drift;
        const double swing =
            (g_swing && (step_index % 2u) == 1u)
                ? step_seconds * HUMAN_SWING
                : 0.0;
        const double placed = cursor + swing;
        size_t at;

        /* Each voice is humanized independently: a drummer's kick and hat do
         * not share a timing error. */
        if (pattern->kick[s]) {
            at = (size_t)fmax(0.0, (placed + human_jitter()) * SAMPLE_RATE);
            add_kick(out, total, at, human_gain(0.85f, step_index));
        }
        if (pattern->snare[s]) {
            at = (size_t)fmax(0.0, (placed + human_jitter()) * SAMPLE_RATE);
            add_snare(out, total, at, human_gain(0.55f, step_index));
        }
        if (pattern->hat[s]) {
            at = (size_t)fmax(0.0, (placed + human_jitter()) * SAMPLE_RATE);
            add_hat(out, total, at, human_gain(0.30f, step_index), 0);
        }
        if (pattern->open_hat[s]) {
            at = (size_t)fmax(0.0, (placed + human_jitter()) * SAMPLE_RATE);
            add_hat(out, total, at, human_gain(0.26f, step_index), 1);
        }
        if (pattern->bass[s] >= 0) {
            const double root = 55.0;  /* A1 */
            const double f = root *
                pow(2.0, (double)semitone_offsets[pattern->bass[s]] / 12.0);
            at = (size_t)fmax(0.0, (placed + human_jitter()) * SAMPLE_RATE);
            add_bass(out, total, at, human_gain(0.45f, step_index),
                     f, step_seconds * 3.5);
        }

        step_index += 1u;
        cursor += step_seconds;
    }

    /* Normalize to just under full scale. Summed layers overshoot 1.0, and
     * clipping would add broadband distortion that changes the onset structure
     * the estimator reads -- a corpus artefact, not a property of the music. */
    {
        float peak = 0.0f;
        size_t i;

        for (i = 0u; i < total; ++i) {
            const float magnitude = out[i] < 0.0f ? -out[i] : out[i];
            if (magnitude > peak) {
                peak = magnitude;
            }
        }
        if (peak > 1e-6f) {
            const float scale = 0.89f / peak;
            for (i = 0u; i < total; ++i) {
                out[i] *= scale;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* relation classification                                              */

typedef enum {
    REL_EXACT = 0,
    REL_HALF,
    REL_DOUBLE,
    REL_THIRD,
    REL_TRIPLE,
    REL_TWO_THIRDS,
    REL_THREE_HALF,
    REL_QUARTER,
    REL_QUADRUPLE,
    REL_OTHER,
    REL_ABSENT,
    REL_COUNT
} relation_t;

static const char *relation_name(relation_t r)
{
    switch (r) {
    case REL_EXACT:      return "exact";
    case REL_HALF:       return "half";
    case REL_DOUBLE:     return "double";
    case REL_THIRD:      return "third";
    case REL_TRIPLE:     return "triple";
    case REL_TWO_THIRDS: return "two-thirds";
    case REL_THREE_HALF: return "three-halves";
    case REL_QUARTER:    return "quarter";
    case REL_QUADRUPLE:  return "quadruple";
    case REL_ABSENT:     return "ABSENT";
    default:             return "OTHER";
    }
}

/*
 * Two tolerances, because they answer different questions.
 *
 * EXACT decides whether a host could beat-match on this answer. Four percent
 * is far too loose for that: 130.8 BPM against a true 135 drifts a whole beat
 * in about thirty seconds. One percent is roughly the point where a
 * constant-tempo grid stays usable across a mix.
 *
 * The octave-family ratios only need to identify which relation was hit, and
 * there the wide tolerance is right -- a half-tempo answer is a half-tempo
 * answer whether it is out by one percent or three.
 */
#define EXACT_TOLERANCE  0.01
#define FAMILY_TOLERANCE 0.04

static int near_ratio(double value, double target)
{
    const double tolerance =
        target == 1.0 ? EXACT_TOLERANCE : FAMILY_TOLERANCE;
    return fabs(value - target) <= target * tolerance;
}

static relation_t classify(uint32_t reported_millibpm, uint32_t truth_millibpm)
{
    const double ratio =
        truth_millibpm != 0u
            ? (double)reported_millibpm / (double)truth_millibpm
            : 0.0;

    if (reported_millibpm == 0u) return REL_ABSENT;
    if (near_ratio(ratio, 1.0))       return REL_EXACT;
    if (near_ratio(ratio, 0.5))       return REL_HALF;
    if (near_ratio(ratio, 2.0))       return REL_DOUBLE;
    if (near_ratio(ratio, 1.0 / 3.0)) return REL_THIRD;
    if (near_ratio(ratio, 3.0))       return REL_TRIPLE;
    if (near_ratio(ratio, 2.0 / 3.0)) return REL_TWO_THIRDS;
    if (near_ratio(ratio, 1.5))       return REL_THREE_HALF;
    if (near_ratio(ratio, 0.25))      return REL_QUARTER;
    if (near_ratio(ratio, 4.0))       return REL_QUADRUPLE;
    return REL_OTHER;
}

static int is_octave_error(relation_t r)
{
    return r == REL_HALF || r == REL_DOUBLE || r == REL_THIRD ||
           r == REL_TRIPLE || r == REL_TWO_THIRDS || r == REL_THREE_HALF ||
           r == REL_QUARTER || r == REL_QUADRUPLE;
}

/* ------------------------------------------------------------------ */
/* analysis                                                             */

typedef struct {
    uint32_t reported_millibpm;
    uint32_t confidence;
    apta_feature_state_t state;
    /* Candidate evidence, so the confidence formula's inputs can be inspected
     * from outside: 1 - score[1]/score[0] is exactly its `separation` term. */
    uint32_t candidate_count;
    float separation;
    int ok;
} analysis_t;

static analysis_t analyze(const float *audio, size_t frames)
{
    const apta_feature_mask_t features =
        APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_BPM |
        APTA_FEATURE_LOCAL_BEATGRID | APTA_FEATURE_CONFIDENCE;
    apta_context_config_t cc;
    apta_session_config_t sc;
    apta_context_t *ctx = NULL;
    apta_session_t *s = NULL;
    apta_work_budget_t budget;
    apta_tempo_view_t tempo;
    const apta_result_t *result = NULL;
    analysis_t out;
    size_t pushed = 0u;
    apta_status_t st;
    unsigned long guard = 0ul;

    memset(&out, 0, sizeof(out));

    apta_context_config_init(&cc);
    cc.requested_capabilities = features;
    if (apta_context_create(&cc, &ctx) < 0) {
        return out;
    }

    apta_session_config_init(&sc);
    sc.source_sample_rate = SAMPLE_RATE;
    sc.channel_count = 1u;
    sc.sample_format = APTA_SAMPLE_F32_NATIVE_INTERLEAVED;
    sc.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    sc.total_frames = frames;
    sc.requested_features = features;
    if (apta_session_create(ctx, &sc, &s) < 0) {
        (void)apta_context_destroy(ctx);
        return out;
    }

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = BLOCK_FRAMES;
    budget.maximum_steps = 32u;

    while (pushed < frames) {
        apta_pcm_block_t block;
        uint32_t accepted = 0u;
        uint32_t count = (uint32_t)(frames - pushed);

        if (count > BLOCK_FRAMES) {
            count = BLOCK_FRAMES;
        }
        apta_pcm_block_init(&block);
        block.data = &audio[pushed];
        block.first_frame = pushed;
        block.frame_count = count;
        if (apta_session_push_pcm(s, &block, &accepted) < 0 ||
            accepted != count) {
            goto cleanup;
        }
        pushed += count;
        st = apta_session_process(s, &budget, NULL);
        if (st < 0) {
            goto cleanup;
        }
    }

    if (apta_session_signal_end_of_input(s, frames) < 0) {
        goto cleanup;
    }
    for (;;) {
        st = apta_session_process(s, &budget, NULL);
        guard += 1ul;
        if (st == APTA_STATUS_END_OF_INPUT) break;
        if (st < 0 || guard > 100000ul) goto cleanup;
    }

    result = apta_session_acquire_result(s);
    if (result == NULL) {
        goto cleanup;
    }
    apta_tempo_view_init(&tempo);
    if (apta_result_get_tempo(result, NULL, &tempo) == APTA_STATUS_OK) {
        out.reported_millibpm = tempo.selected.tempo_millibpm;
        out.confidence = tempo.selected.confidence;
        out.state = tempo.selected.state;
        out.candidate_count = tempo.candidate_count;
        out.separation =
            (tempo.candidate_count > 1u &&
             tempo.candidates[0].score != 0u)
                ? 1.0f - (float)tempo.candidates[1].score /
                             (float)tempo.candidates[0].score
                : 1.0f;
        out.ok = 1;
    }
    apta_result_release(result);

cleanup:
    (void)apta_session_destroy(s);
    (void)apta_context_destroy(ctx);
    return out;
}

/* ------------------------------------------------------------------ */
/* real recordings                                                      */

/* Analyse a decoded file in pull mode. Mirrors analyze() above, but the source
 * geometry comes from the file rather than from the synthesizer. */
static analysis_t analyze_file(const char *path)
{
    const apta_feature_mask_t features =
        APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_BPM |
        APTA_FEATURE_LOCAL_BEATGRID | APTA_FEATURE_CONFIDENCE;
    apta_decoder_t decoder;
    apta_decoder_info_t decoder_info;
    apta_pcm_source_t source;
    apta_context_config_t cc;
    apta_session_config_t sc;
    apta_context_t *ctx = NULL;
    apta_session_t *s = NULL;
    apta_work_budget_t budget;
    apta_tempo_view_t tempo;
    const apta_result_t *result = NULL;
    analysis_t out;
    apta_status_t st;
    unsigned long guard = 0ul;

    memset(&out, 0, sizeof(out));
    apta_decoder_init(&decoder);
    apta_decoder_info_init(&decoder_info);
    if (apta_wav_decoder_open_path(path, &decoder, &decoder_info) < 0) {
        return out;
    }
    apta_pcm_source_init(&source);
    if (apta_decoder_make_pcm_source(&decoder, &source) < 0) {
        goto close_decoder;
    }

    apta_context_config_init(&cc);
    cc.requested_capabilities = features;
    if (apta_context_create(&cc, &ctx) < 0) {
        goto close_decoder;
    }

    apta_session_config_init(&sc);
    sc.input_mode = APTA_INPUT_MODE_PULL;
    sc.source_sample_rate = decoder_info.sample_rate;
    sc.channel_count = decoder_info.channel_count;
    sc.sample_format = decoder_info.sample_format;
    sc.channel_layout = decoder_info.channel_layout;
    sc.total_frames = decoder_info.total_frames;
    sc.requested_features = features;
    if (apta_session_create(ctx, &sc, &s) < 0) {
        goto destroy_context;
    }
    if (apta_session_set_source(s, &source) < 0) {
        goto destroy_session;
    }

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = BLOCK_FRAMES;
    budget.maximum_steps = 32u;
    for (;;) {
        st = apta_session_process(s, &budget, NULL);
        guard += 1ul;
        if (st == APTA_STATUS_END_OF_INPUT) {
            break;
        }
        if (st < 0 || guard > 5000000ul) {
            goto destroy_session;
        }
    }

    result = apta_session_acquire_result(s);
    if (result != NULL) {
        apta_tempo_view_init(&tempo);
        if (apta_result_get_tempo(result, NULL, &tempo) == APTA_STATUS_OK) {
            out.reported_millibpm = tempo.selected.tempo_millibpm;
            out.confidence = tempo.selected.confidence;
            out.state = tempo.selected.state;
            out.candidate_count = tempo.candidate_count;
            out.separation =
                (tempo.candidate_count > 1u &&
                 tempo.candidates[0].score != 0u)
                    ? 1.0f - (float)tempo.candidates[1].score /
                                 (float)tempo.candidates[0].score
                    : 1.0f;
            out.ok = 1;
        }
        apta_result_release(result);
    }

destroy_session:
    (void)apta_session_destroy(s);
destroy_context:
    (void)apta_context_destroy(ctx);
close_decoder:
    apta_decoder_close(&decoder);
    return out;
}

/* ------------------------------------------------------------------ */
/* optional WAV output so the corpus can be inspected                   */

static void write_le32(FILE *f, uint32_t v)
{
    fputc((int)(v & 0xffu), f);
    fputc((int)((v >> 8) & 0xffu), f);
    fputc((int)((v >> 16) & 0xffu), f);
    fputc((int)((v >> 24) & 0xffu), f);
}

static void write_le16(FILE *f, uint16_t v)
{
    fputc((int)(v & 0xffu), f);
    fputc((int)((v >> 8) & 0xffu), f);
}

static void write_wav(const char *path, const float *audio, size_t frames)
{
    FILE *f = fopen(path, "wb");
    size_t i;

    if (f == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        return;
    }
    fwrite("RIFF", 1u, 4u, f);
    write_le32(f, (uint32_t)(36u + frames * 2u));
    fwrite("WAVEfmt ", 1u, 8u, f);
    write_le32(f, 16u);
    write_le16(f, 1u);
    write_le16(f, 1u);
    write_le32(f, SAMPLE_RATE);
    write_le32(f, SAMPLE_RATE * 2u);
    write_le16(f, 2u);
    write_le16(f, 16u);
    fwrite("data", 1u, 4u, f);
    write_le32(f, (uint32_t)(frames * 2u));
    for (i = 0u; i < frames; ++i) {
        float v = audio[i];
        int32_t q;
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        q = (int32_t)(v * 32767.0f);
        write_le16(f, (uint16_t)(int16_t)q);
    }
    fclose(f);
}

/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t confidence;
    int exact;
} corpus_result_t;

static corpus_result_t g_results[256];
static unsigned result_count;

/* Shared bookkeeping, so the synthetic and real paths report identically. */
static uint32_t g_total;
static uint32_t g_counts[REL_COUNT];
static uint32_t g_correct_sum, g_correct_n, g_correct_min = 255u, g_correct_max;
static uint32_t g_wrong_sum, g_wrong_n, g_wrong_min = 255u, g_wrong_max;
static uint32_t g_high_confidence_errors;
static uint32_t g_high_confidence_octave_errors;

/* `truth_millibpm` allows fractional ground truth such as 174.5 BPM. */
static void record(const char *label,
                   uint32_t truth_millibpm,
                   analysis_t a,
                   int verbose)
{
    const relation_t rel =
        a.ok ? classify(a.reported_millibpm, truth_millibpm) : REL_ABSENT;

    g_counts[rel] += 1u;
    g_total += 1u;
    if (result_count < sizeof(g_results) / sizeof(g_results[0])) {
        g_results[result_count].confidence = a.confidence;
        g_results[result_count].exact = (rel == REL_EXACT);
        result_count += 1u;
    }

    if (rel == REL_EXACT) {
        g_correct_sum += a.confidence;
        g_correct_n += 1u;
        if (a.confidence < g_correct_min) g_correct_min = a.confidence;
        if (a.confidence > g_correct_max) g_correct_max = a.confidence;
    } else {
        g_wrong_sum += a.confidence;
        g_wrong_n += 1u;
        if (a.confidence < g_wrong_min) g_wrong_min = a.confidence;
        if (a.confidence > g_wrong_max) g_wrong_max = a.confidence;
        if (a.confidence >= ACTIONABLE_CONFIDENCE &&
            a.confidence != APTA_CONFIDENCE_UNKNOWN) {
            g_high_confidence_errors += 1u;
            if (is_octave_error(rel)) {
                g_high_confidence_octave_errors += 1u;
            }
        }
    }

    if (verbose || rel != REL_EXACT) {
        printf("%-34s %8.3f  %10.3f  %-13s %5u %d  n=%u sep=%.2f\n",
               label, (double)truth_millibpm / 1000.0,
               (double)a.reported_millibpm / 1000.0,
               relation_name(rel), a.confidence, (int)a.state,
               a.candidate_count, (double)a.separation);
    }
}

/* Run every entry of a "path<space>bpm" list. Returns non-zero on a file the
 * list names but the decoder cannot open, because a silently skipped track
 * would quietly bias the rates. */
static int run_track_list(const char *list_path, int verbose)
{
    char line[1024];
    FILE *list;
    int failures = 0;

    list = fopen(list_path, "r");
    if (list == NULL) {
        fprintf(stderr, "cannot open track list %s\n", list_path);
        return 1;
    }
    while (fgets(line, sizeof(line), list) != NULL) {
        char *cursor = line;
        char *path;
        char *tempo_text;
        double truth;
        analysis_t a;

        while (*cursor == ' ' || *cursor == '\t') {
            cursor += 1;
        }
        if (*cursor == '#' || *cursor == '\n' || *cursor == '\r' ||
            *cursor == '\0') {
            continue;
        }
        /* Split on the last run of whitespace, so paths may contain spaces. */
        cursor[strcspn(cursor, "\r\n")] = '\0';
        tempo_text = strrchr(cursor, ' ');
        if (tempo_text == NULL) {
            tempo_text = strrchr(cursor, '\t');
        }
        if (tempo_text == NULL) {
            fprintf(stderr, "malformed line (no tempo): %s\n", cursor);
            failures += 1;
            continue;
        }
        *tempo_text = '\0';
        tempo_text += 1;
        path = cursor;
        while (*path == ' ' || *path == '\t') {
            path += 1;
        }
        truth = strtod(tempo_text, NULL);
        if (truth <= 0.0) {
            fprintf(stderr, "malformed line (bad tempo): %s\n", tempo_text);
            failures += 1;
            continue;
        }

        a = analyze_file(path);
        if (!a.ok) {
            fprintf(stderr, "could not analyse %s\n", path);
            failures += 1;
            continue;
        }
        record(path, (uint32_t)(truth * 1000.0 + 0.5), a, verbose);
    }
    fclose(list);
    return failures;
}

int main(int argc, char **argv)
{
    uint32_t seconds = DEFAULT_SECONDS;
    const char *wav_dir = NULL;
    const char *tracks_path = NULL;
    int verbose = 0;
    size_t frames;
    float *audio;
    size_t pattern_index;
    size_t tempo_index;
    int arg;

    for (arg = 1; arg < argc; ++arg) {
        if (strcmp(argv[arg], "--seconds") == 0 && arg + 1 < argc) {
            seconds = (uint32_t)strtoul(argv[++arg], NULL, 10);
        } else if (strcmp(argv[arg], "--write-wav") == 0 && arg + 1 < argc) {
            wav_dir = argv[++arg];
        } else if (strcmp(argv[arg], "--tracks") == 0 && arg + 1 < argc) {
            tracks_path = argv[++arg];
        } else if (strcmp(argv[arg], "--humanize") == 0) {
            g_jitter = g_dynamics = g_drift = g_swing = 1;
        } else if (strcmp(argv[arg], "--jitter") == 0) {
            g_jitter = 1;
        } else if (strcmp(argv[arg], "--dynamics") == 0) {
            g_dynamics = 1;
        } else if (strcmp(argv[arg], "--drift") == 0) {
            g_drift = 1;
        } else if (strcmp(argv[arg], "--swing") == 0) {
            g_swing = 1;
        } else if (strcmp(argv[arg], "--verbose") == 0) {
            verbose = 1;
        } else {
            fprintf(stderr,
                    "usage: %s [--seconds N] [--write-wav DIR] [--verbose]\n"
                    "       realism: --humanize | any of --jitter"
                    " --dynamics --drift --swing\n"
                    "       %s --tracks LIST [--verbose]\n",
                    argv[0], argv[0]);
            return 2;
        }
    }

    setvbuf(stdout, NULL, _IONBF, 0);

    printf("APTA tempo accuracy, %s corpus\n",
           tracks_path != NULL ? "REAL" : "SYNTHETIC");
    if (tracks_path == NULL) {
        printf("%u patterns x %u tempi, %u s each at %u Hz mono\n",
               (unsigned)PATTERN_COUNT, (unsigned)TEMPO_COUNT, seconds,
               SAMPLE_RATE);
        printf("timing:%s%s%s%s\n\n",
               g_jitter ? " jitter(6ms)" : "",
               g_dynamics ? " dynamics" : "",
               g_drift ? " drift(0.5%)" : "",
               (g_jitter || g_dynamics || g_drift || g_swing)
                   ? (g_swing ? " swing(15%)" : "")
                   : " exact, every onset on the grid");
    } else {
        printf("track list: %s\n\n", tracks_path);
    }
    printf("%-34s %8s  %10s  %-13s %5s %s\n",
           tracks_path != NULL ? "track" : "pattern",
           "truth", "reported", "relation", "conf", "state");
    printf("---------------------------------------------------------------"
           "----------------\n");

    if (tracks_path != NULL) {
        const int failures = run_track_list(tracks_path, verbose);

        if (g_total == 0u) {
            puts("\nno tracks analysed");
            return 1;
        }
        if (failures != 0) {
            printf("\n%d track(s) could not be analysed and are excluded;\n"
                   "the rates below describe only what was analysed.\n",
                   failures);
        }
    } else {
        frames = (size_t)SAMPLE_RATE * seconds;
        audio = (float *)malloc(frames * sizeof(*audio));
        if (audio == NULL) {
            puts("allocation failed");
            return 1;
        }

        for (pattern_index = 0u;
             pattern_index < PATTERN_COUNT;
             ++pattern_index) {
            const pattern_t *pattern = &g_patterns[pattern_index];

            for (tempo_index = 0u; tempo_index < TEMPO_COUNT; ++tempo_index) {
                const uint32_t bpm = g_tempos[tempo_index];

                render(pattern, bpm, audio, frames);
                if (wav_dir != NULL) {
                    char path[512];
                    (void)snprintf(path, sizeof(path), "%s/%s_%03u.wav",
                                   wav_dir, pattern->name, bpm);
                    write_wav(path, audio, frames);
                }
                record(pattern->name, bpm * 1000u,
                       analyze(audio, frames), verbose);
            }
        }
        free(audio);
    }

    printf("\n=== summary (%s corpus) ===\n",
           tracks_path != NULL ? "REAL" : "SYNTHETIC");
    printf("tracks                     %u\n", g_total);
    printf("exact                      %u (%.1f%%)\n",
           g_counts[REL_EXACT], 100.0 * g_counts[REL_EXACT] / (double)g_total);
    {
        uint32_t octave = 0u;
        int i;
        for (i = 0; i < REL_COUNT; ++i) {
            if (is_octave_error((relation_t)i)) octave += g_counts[i];
        }
        printf("octave-family errors       %u (%.1f%%)\n",
               octave, 100.0 * octave / (double)g_total);
    }
    printf("other errors               %u\n", g_counts[REL_OTHER]);
    printf("no tempo reported          %u\n", g_counts[REL_ABSENT]);

    printf("\nbreakdown:\n");
    {
        int i;
        for (i = 0; i < REL_COUNT; ++i) {
            if (g_counts[i] != 0u) {
                printf("  %-14s %u\n", relation_name((relation_t)i), g_counts[i]);
            }
        }
    }

    printf("\nconfidence distribution:\n");
    if (g_correct_n != 0u) {
        printf("  correct   n=%-3u mean=%5.1f min=%3u max=%3u\n",
               g_correct_n,
               (double)g_correct_sum / g_correct_n,
               g_correct_min, g_correct_max);
    } else {
        printf("  correct   n=0\n");
    }
    if (g_wrong_n != 0u) {
        printf("  incorrect n=%-3u mean=%5.1f min=%3u max=%3u\n",
               g_wrong_n,
               (double)g_wrong_sum / g_wrong_n,
               g_wrong_min, g_wrong_max);
    } else {
        printf("  incorrect n=0\n");
    }

    /* B1 asks for a threshold to be defined explicitly and reported against.
     * The useful question is not one number but whether any threshold
     * separates correct from incorrect at all, so sweep it. */
    printf("\nthreshold sweep (how many of each survive a >= gate):\n");
    printf("  %-10s %-18s %-18s %s\n",
           "threshold", "correct admitted", "wrong admitted", "usable");
    {
        static const uint32_t gates[] = {
            50u, 55u, 60u, 65u, 70u, 75u, 80u, 85u, 90u, 95u
        };
        unsigned g;

        for (g = 0u; g < sizeof(gates) / sizeof(gates[0]); ++g) {
            uint32_t ok = 0u;
            uint32_t bad = 0u;
            unsigned k;

            for (k = 0u; k < result_count; ++k) {
                if (g_results[k].confidence >= gates[g] &&
                    g_results[k].confidence != APTA_CONFIDENCE_UNKNOWN) {
                    if (g_results[k].exact) {
                        ok += 1u;
                    } else {
                        bad += 1u;
                    }
                }
            }
            printf("  %-10u %-18u %-18u %s\n",
                   gates[g], ok, bad,
                   (bad == 0u && ok > 0u) ? "YES" : "no");
        }
    }

    printf("\nactionable threshold = %u\n", ACTIONABLE_CONFIDENCE);
    printf("high-confidence errors        %u\n", g_high_confidence_errors);
    printf("high-confidence octave errors %u   <-- B1 requires this to be 0\n",
           g_high_confidence_octave_errors);

    if (tracks_path != NULL) {
        printf("\nNOTE: real recordings. Ground truth is whatever the track\n"
               "list declares, so a wrong annotation reads as an estimator\n"
               "error. Check outliers against the file before believing them.\n");
    } else {
        printf("\nNOTE: synthetic material%s. Still no production\n"
               "processing, so these rates remain a floor rather than a\n"
               "prediction of real-world accuracy.\n",
               (g_jitter || g_dynamics || g_drift || g_swing)
                   ? ", with the realism knobs listed above"
                   : " with exact timing and no dynamics");
    }

    return 0;
}
