// SPDX-License-Identifier: Apache-2.0
/* Test-process-only observer; never part of a session or production library. */
static struct {
    float energy[APTA_INTERNAL_KEY_EVIDENCE_VARIANTS][36];
    float compressed[APTA_INTERNAL_KEY_EVIDENCE_VARIANTS][36];
    unsigned char seen[APTA_INTERNAL_KEY_EVIDENCE_VARIANTS][36];
    float raw_folded[12], compressed_window[12], replay_cumulative[12];
    unsigned count, failed;
} contrast;

static void contrast_reset(void) { memset(&contrast, 0, sizeof(contrast)); }

void apta_key_contrast_observe_energy(uint32_t variant, uint32_t bin, float energy, float compressed)
{
    if (variant >= APTA_INTERNAL_KEY_EVIDENCE_VARIANTS || bin >= 36u ||
        !isfinite(energy) || energy < 0.0f || !isfinite(compressed) || compressed < 0.0f) {
        contrast.failed = 1u;
        return;
    }
    if (contrast.seen[variant][bin]) contrast.failed = 1u;
    contrast.seen[variant][bin] = 1u;
    contrast.energy[variant][bin] = energy;
    contrast.compressed[variant][bin] = compressed;
    ++contrast.count;
}

static int contrast_finish(const float *native_cumulative)
{
    unsigned bin, variant;
    CHECK(!contrast.failed && contrast.count == APTA_INTERNAL_KEY_EVIDENCE_VARIANTS * 36u);
    memset(contrast.raw_folded, 0, sizeof(contrast.raw_folded));
    memset(contrast.compressed_window, 0, sizeof(contrast.compressed_window));
    for (bin = 0; bin < 36; ++bin) {
        float energy = 0.0f, compressed = 0.0f;
        for (variant = 0; variant < APTA_INTERNAL_KEY_EVIDENCE_VARIANTS; ++variant) {
            CHECK(contrast.seen[variant][bin]);
            energy += contrast.energy[variant][bin];
            compressed += contrast.compressed[variant][bin];
        }
        energy /= (float)APTA_INTERNAL_KEY_EVIDENCE_VARIANTS;
        compressed /= (float)APTA_INTERNAL_KEY_EVIDENCE_VARIANTS;
        contrast.raw_folded[bin % 12u] += energy;
        contrast.compressed_window[bin % 12u] += compressed;
        contrast.replay_cumulative[bin % 12u] += compressed;
    }
    /* Default has one variant; band averages probes before each bin addition.
     * Preserve those operations, not regrouped vector sums, in this replay. */
    CHECK(memcmp(native_cumulative, contrast.replay_cumulative, 12 * sizeof(float)) == 0);
    return 0;
}

static void contrast_vector(const char *name, const float *values, unsigned count)
{
    unsigned i;
    printf("\"%s\":[", name);
    for (i = 0; i < count; ++i) printf("%s%.9g", i ? "," : "", (double)values[i]);
    printf("]");
}

static void contrast_print(void)
{
    unsigned variant;
    printf(",\"contrast\":{\"variants\":%u,\"observations\":%u,\"raw_energy_by_variant\":[",
           (unsigned)APTA_INTERNAL_KEY_EVIDENCE_VARIANTS, contrast.count);
    for (variant = 0; variant < APTA_INTERNAL_KEY_EVIDENCE_VARIANTS; ++variant) {
        unsigned bin;
        printf("%s[", variant ? "," : "");
        for (bin = 0; bin < 36; ++bin)
            printf("%s%.9g", bin ? "," : "", (double)contrast.energy[variant][bin]);
        printf("]");
    }
    printf("],\"compressed_by_variant\":[");
    for (variant = 0; variant < APTA_INTERNAL_KEY_EVIDENCE_VARIANTS; ++variant) {
        unsigned bin;
        printf("%s[", variant ? "," : "");
        for (bin = 0; bin < 36; ++bin)
            printf("%s%.9g", bin ? "," : "", (double)contrast.compressed[variant][bin]);
        printf("]");
    }
    printf("],");
    contrast_vector("raw_folded", contrast.raw_folded, 12);
    printf(",");
    contrast_vector("compressed_window", contrast.compressed_window, 12);
    printf(",\"native_cumulative_bit_identical\":true}");
}

static void contrast_next_window(void)
{
    memset(contrast.seen, 0, sizeof(contrast.seen));
    contrast.count = 0u;
}
