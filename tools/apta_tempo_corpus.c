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
 *   ./build/apta-tempo-corpus [--seconds N] [--write-wav DIR] [--verbose]
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

#define SAMPLE_RATE 44100u
#define DEFAULT_SECONDS 30u
#define BLOCK_FRAMES 1024u

/* Confidence at or above this is "the host would act on it": Sync and Quantize
 * are gated on confidence, and a confidently wrong grid is worse than an
 * absent one. Stated explicitly because B1's acceptance asks for a threshold. */
#define ACTIONABLE_CONFIDENCE 70u

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

static void render(const pattern_t *pattern,
                   uint32_t bpm,
                   float *out,
                   size_t total)
{
    const double step_seconds = 60.0 / (double)bpm / 4.0;  /* 16th note */
    const size_t step_frames = (size_t)(step_seconds * SAMPLE_RATE);
    size_t step_index = 0u;
    size_t at = 0u;

    rng_seed(0x2468aceu ^ bpm);
    memset(out, 0, total * sizeof(*out));

    while (at < total) {
        const uint32_t s = (uint32_t)(step_index % 16u);

        if (pattern->kick[s]) {
            add_kick(out, total, at, 0.85f);
        }
        if (pattern->snare[s]) {
            add_snare(out, total, at, 0.55f);
        }
        if (pattern->hat[s]) {
            add_hat(out, total, at, 0.30f, 0);
        }
        if (pattern->open_hat[s]) {
            add_hat(out, total, at, 0.26f, 1);
        }
        if (pattern->bass[s] >= 0) {
            const double root = 55.0;  /* A1 */
            const double f = root *
                pow(2.0, (double)semitone_offsets[pattern->bass[s]] / 12.0);
            add_bass(out, total, at, 0.45f, f, step_seconds * 3.5);
        }

        step_index += 1u;
        at += step_frames;
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

static int near_ratio(double value, double target)
{
    return fabs(value - target) <= target * 0.04;
}

static relation_t classify(uint32_t reported_millibpm, uint32_t truth_bpm)
{
    const double ratio =
        (double)reported_millibpm / 1000.0 / (double)truth_bpm;

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
        out.ok = 1;
    }
    apta_result_release(result);

cleanup:
    (void)apta_session_destroy(s);
    (void)apta_context_destroy(ctx);
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

int main(int argc, char **argv)
{
    uint32_t seconds = DEFAULT_SECONDS;
    const char *wav_dir = NULL;
    int verbose = 0;
    size_t frames;
    float *audio;
    size_t pattern_index;
    size_t tempo_index;
    uint32_t total = 0u;
    uint32_t counts[REL_COUNT];
    uint32_t correct_conf_sum = 0u, correct_conf_n = 0u;
    uint32_t correct_conf_min = 255u, correct_conf_max = 0u;
    uint32_t wrong_conf_sum = 0u, wrong_conf_n = 0u;
    uint32_t wrong_conf_min = 255u, wrong_conf_max = 0u;
    uint32_t high_confidence_octave_errors = 0u;
    uint32_t high_confidence_any_errors = 0u;
    int arg;

    for (arg = 1; arg < argc; ++arg) {
        if (strcmp(argv[arg], "--seconds") == 0 && arg + 1 < argc) {
            seconds = (uint32_t)strtoul(argv[++arg], NULL, 10);
        } else if (strcmp(argv[arg], "--write-wav") == 0 && arg + 1 < argc) {
            wav_dir = argv[++arg];
        } else if (strcmp(argv[arg], "--verbose") == 0) {
            verbose = 1;
        } else {
            fprintf(stderr,
                    "usage: %s [--seconds N] [--write-wav DIR] [--verbose]\n",
                    argv[0]);
            return 2;
        }
    }

    setvbuf(stdout, NULL, _IONBF, 0);
    memset(counts, 0, sizeof(counts));

    frames = (size_t)SAMPLE_RATE * seconds;
    audio = (float *)malloc(frames * sizeof(*audio));
    if (audio == NULL) {
        puts("allocation failed");
        return 1;
    }

    printf("APTA tempo accuracy, SYNTHETIC corpus\n");
    printf("%u patterns x %u tempi, %u s each at %u Hz mono\n\n",
           (unsigned)PATTERN_COUNT, (unsigned)TEMPO_COUNT, seconds,
           SAMPLE_RATE);
    printf("%-16s %6s  %10s  %-13s %5s %s\n",
           "pattern", "truth", "reported", "relation", "conf", "state");
    printf("---------------------------------------------------------------"
           "-------\n");

    for (pattern_index = 0u; pattern_index < PATTERN_COUNT; ++pattern_index) {
        const pattern_t *pattern = &g_patterns[pattern_index];

        for (tempo_index = 0u; tempo_index < TEMPO_COUNT; ++tempo_index) {
            const uint32_t bpm = g_tempos[tempo_index];
            analysis_t a;
            relation_t rel;

            render(pattern, bpm, audio, frames);

            if (wav_dir != NULL) {
                char path[512];
                (void)snprintf(path, sizeof(path), "%s/%s_%03u.wav",
                               wav_dir, pattern->name, bpm);
                write_wav(path, audio, frames);
            }

            a = analyze(audio, frames);
            rel = a.ok ? classify(a.reported_millibpm, bpm) : REL_ABSENT;
            counts[rel] += 1u;
            total += 1u;

            if (rel == REL_EXACT) {
                correct_conf_sum += a.confidence;
                correct_conf_n += 1u;
                if (a.confidence < correct_conf_min) correct_conf_min = a.confidence;
                if (a.confidence > correct_conf_max) correct_conf_max = a.confidence;
            } else {
                wrong_conf_sum += a.confidence;
                wrong_conf_n += 1u;
                if (a.confidence < wrong_conf_min) wrong_conf_min = a.confidence;
                if (a.confidence > wrong_conf_max) wrong_conf_max = a.confidence;
                if (a.confidence >= ACTIONABLE_CONFIDENCE &&
                    a.confidence != APTA_CONFIDENCE_UNKNOWN) {
                    high_confidence_any_errors += 1u;
                    if (is_octave_error(rel)) {
                        high_confidence_octave_errors += 1u;
                    }
                }
            }

            if (verbose || rel != REL_EXACT) {
                printf("%-16s %6u  %10.3f  %-13s %5u %d\n",
                       pattern->name, bpm,
                       (double)a.reported_millibpm / 1000.0,
                       relation_name(rel), a.confidence, (int)a.state);
            }
        }
    }

    printf("\n=== summary (SYNTHETIC corpus) ===\n");
    printf("tracks                     %u\n", total);
    printf("exact                      %u (%.1f%%)\n",
           counts[REL_EXACT], 100.0 * counts[REL_EXACT] / (double)total);
    {
        uint32_t octave = 0u;
        int i;
        for (i = 0; i < REL_COUNT; ++i) {
            if (is_octave_error((relation_t)i)) octave += counts[i];
        }
        printf("octave-family errors       %u (%.1f%%)\n",
               octave, 100.0 * octave / (double)total);
    }
    printf("other errors               %u\n", counts[REL_OTHER]);
    printf("no tempo reported          %u\n", counts[REL_ABSENT]);

    printf("\nbreakdown:\n");
    {
        int i;
        for (i = 0; i < REL_COUNT; ++i) {
            if (counts[i] != 0u) {
                printf("  %-14s %u\n", relation_name((relation_t)i), counts[i]);
            }
        }
    }

    printf("\nconfidence distribution:\n");
    if (correct_conf_n != 0u) {
        printf("  correct   n=%-3u mean=%5.1f min=%3u max=%3u\n",
               correct_conf_n,
               (double)correct_conf_sum / correct_conf_n,
               correct_conf_min, correct_conf_max);
    } else {
        printf("  correct   n=0\n");
    }
    if (wrong_conf_n != 0u) {
        printf("  incorrect n=%-3u mean=%5.1f min=%3u max=%3u\n",
               wrong_conf_n,
               (double)wrong_conf_sum / wrong_conf_n,
               wrong_conf_min, wrong_conf_max);
    } else {
        printf("  incorrect n=0\n");
    }

    printf("\nactionable threshold = %u\n", ACTIONABLE_CONFIDENCE);
    printf("high-confidence errors        %u\n", high_confidence_any_errors);
    printf("high-confidence octave errors %u   <-- B1 requires this to be 0\n",
           high_confidence_octave_errors);

    printf("\nNOTE: synthetic material with exact timing and no production\n"
           "processing. These rates are a floor, not a prediction of\n"
           "real-world accuracy.\n");

    free(audio);
    return 0;
}
