// SPDX-License-Identifier: Apache-2.0
/* Private instrument: direct complex projection plus double recurrence check. */
#define REF_VARIANTS APTA_INTERNAL_KEY_EVIDENCE_VARIANTS
typedef struct {
    double co, si, re, im, real_sum, imag_sum, q1, q2;
} reference_bin;
static struct {
    reference_bin bins[2][REF_VARIANTS][36];
    double window[2][12], cumulative[2][12], energies[2][REF_VARIANTS][36];
    double double_sum, max_identity_error;
    float float_sum;
    unsigned count, samples, initialized;
} extraction;

static void reference_reset(void) { memset(&extraction, 0, sizeof(extraction)); }

static void reference_initialize(const apta_session_t *session)
{
    static const float frequencies[36] = {
        130.8128f,138.5913f,146.8324f,155.5635f,164.8138f,174.6141f,
        184.9972f,195.9977f,207.6523f,220.0000f,233.0819f,246.9417f,
        261.6256f,277.1826f,293.6648f,311.1270f,329.6276f,349.2282f,
        369.9944f,391.9954f,415.3047f,440.0000f,466.1638f,493.8833f,
        523.2511f,554.3653f,587.3295f,622.2540f,659.2551f,698.4565f,
        739.9888f,783.9909f,830.6094f,880.0000f,932.3275f,987.7666f};
#ifdef APTA_INTERNAL_KEY_SEMITONE_BAND
    static const float ratios[REF_VARIANTS] = {0.98093009f, 1.0f, 1.01944064f};
#else
    static const float ratios[REF_VARIANTS] = {1.0f};
#endif
    unsigned r, v, b;
    for (r = 0; r < 2; ++r) for (v = 0; v < REF_VARIANTS; ++v) for (b = 0; b < 36; ++b) {
        reference_bin *bin = &extraction.bins[r][v][b];
        double angle = r == 0 ? acos((double)session->key_analysis.coefficients[v][b] / 2.0)
            : 6.2831853071795864769 * (double)frequencies[b] * (double)ratios[v] / (RATE / 4u);
        bin->co = r == 0 ? (double)session->key_analysis.coefficients[v][b] / 2.0 : cos(angle);
        bin->si = sin(angle);
        bin->re = 1.0;
    }
    extraction.initialized = 1u;
}

static int reference_finish(void)
{
    unsigned r, v, b, p;
    memset(extraction.window, 0, sizeof(extraction.window));
    for (r = 0; r < 2; ++r) for (v = 0; v < REF_VARIANTS; ++v) for (b = 0; b < 36; ++b) {
        reference_bin *bin = &extraction.bins[r][v][b];
        double energy = bin->real_sum * bin->real_sum + bin->imag_sum * bin->imag_sum;
        double goertzel = bin->q1 * bin->q1 + bin->q2 * bin->q2 - 2.0 * bin->co * bin->q1 * bin->q2;
        double error = fabs(energy - goertzel) / fmax(1.0, energy);
        CHECK(isfinite(energy) && isfinite(goertzel) && isfinite(error));
        CHECK(error <= 1e-6);
        if (error > extraction.max_identity_error) extraction.max_identity_error = error;
        extraction.energies[r][v][b] = energy;
        extraction.window[r][b % 12u] += log1p(energy) / REF_VARIANTS;
        bin->re = 1.0; bin->im = 0.0;
        bin->real_sum = 0.0; bin->imag_sum = 0.0; bin->q1 = 0.0; bin->q2 = 0.0;
    }
    for (r = 0; r < 2; ++r) for (p = 0; p < 12; ++p)
        extraction.cumulative[r][p] += extraction.window[r][p];
    extraction.samples = 0u;
    return 0;
}

/* Called AFTER native feed, so coefficient initialization has already occurred. */
static int reference_feed(const apta_session_t *session, float sample)
{
    double input[2];
    unsigned r, v, b;
    if (!extraction.initialized) reference_initialize(session);
    extraction.float_sum += sample;
    extraction.double_sum += (double)sample;
    if (++extraction.count < 4u) return 0;
    input[0] = (double)(extraction.float_sum / 4.0f);
    input[1] = extraction.double_sum / 4.0;
    extraction.count = 0u; extraction.float_sum = 0.0f; extraction.double_sum = 0.0;
    for (r = 0; r < 2; ++r) for (v = 0; v < REF_VARIANTS; ++v) for (b = 0; b < 36; ++b) {
        reference_bin *bin = &extraction.bins[r][v][b];
        double next_re = bin->re * bin->co - bin->im * bin->si;
        double next_q = input[r] + 2.0 * bin->co * bin->q1 - bin->q2;
        bin->real_sum += input[r] * bin->re;
        bin->imag_sum += input[r] * bin->im;
        bin->im = bin->im * bin->co + bin->re * bin->si;
        bin->re = next_re;
        bin->q2 = bin->q1; bin->q1 = next_q;
    }
    if (++extraction.samples == RATE / 4u) CHECK(reference_finish() == 0);
    return 0;
}

static int reference_selftest(void)
{
    unsigned stimulus, frame, r, v, b;
    for (stimulus = 0; stimulus < 2; ++stimulus) {
        apta_session_t session;
        memset(&session, 0, sizeof(session));
        session.config.source_sample_rate = RATE;
        session.config.requested_features = APTA_FEATURE_MUSICAL_KEY;
        reference_reset();
        for (frame = 0; frame < RATE; ++frame) {
            float sample = stimulus && frame == 0 ? 1.0f : 0.0f;
            apta_internal_key_feed_sample(&session, sample, frame);
            CHECK(reference_feed(&session, sample) == 0);
        }
        CHECK(session.key_analysis.completed_windows == 1u && extraction.samples == 0u);
        for (r = 0; r < 2; ++r) for (v = 0; v < REF_VARIANTS; ++v) for (b = 0; b < 36; ++b)
            CHECK(fabs(extraction.energies[r][v][b] - (stimulus ? 0.0625 : 0.0)) <= 1e-9);
    }
    reference_reset();
    return 0;
}

static int reference_print(const char *kind, const float *native, unsigned completed)
{
    unsigned r, p;
    if (strncmp(kind, "pcm_", 4) != 0) return 0;
    printf(",\"extraction_reference\":{");
    for (r = 0; r < 2; ++r) {
        const double *chroma = strcmp(kind, "pcm_window") == 0 ? extraction.window[r] : extraction.cumulative[r];
        float rounded[12];
        apta_key_candidate_t candidates[3];
        apta_key_view_t view;
        double error = 0.0, maximum = 0.0;
        for (p = 0; p < 12; ++p) {
            rounded[p] = (float)chroma[p];
            CHECK(isfinite(chroma[p]) && chroma[p] >= 0.0);
            error = fmax(error, fabs(chroma[p] - native[p]));
            maximum = fmax(maximum, chroma[p]);
        }
        CHECK(apta_internal_key_select_chroma(rounded, completed, candidates, &view) == APTA_STATUS_OK);
        printf("%s\"%s\":{\"chroma\":[", r ? "," : "", r ? "nominal" : "effective");
        for (p = 0; p < 12; ++p) printf("%s%.17g", p ? "," : "", chroma[p]);
        printf("],\"selected_tonic\":%u,\"selected_mode\":%u,\"confidence\":%u,"
               "\"max_chroma_error_over_max_reference\":%.17g,\"max_fourier_goertzel_energy_error\":%.17g}",
               (unsigned)view.tonic, mode_index(view.mode), (unsigned)view.confidence,
               error / fmax(1.0, maximum), extraction.max_identity_error);
    }
    printf("}");
    return 0;
}
