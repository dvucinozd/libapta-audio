// SPDX-License-Identifier: Apache-2.0
// Public unchanged-C identity configuration, actual checkpoint and continuation.
#include <apta/apta.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(e) do { if (!(e)) { fprintf(stderr, "line %d: %s\n", __LINE__, #e); exit(2); } } while (0)
static void push(apta_session_t *s, uint64_t first, int16_t value) {
    int16_t pcm[256];
    for (unsigned i = 0; i < 256; ++i) pcm[i] = value;
    apta_pcm_block_t b; apta_pcm_block_init(&b);
    b.first_frame = first; b.frame_count = 256; b.data = pcm;
    uint32_t accepted = 0;
    CHECK(apta_session_push_pcm(s, &b, &accepted) == 0 && accepted == 256);
    apta_work_budget_t budget; apta_work_budget_init(&budget);
    CHECK(apta_session_process(s, &budget, NULL) >= 0);
}
static void identity(apta_session_config_t *c, unsigned kind, unsigned variant) {
    c->source_fingerprint_kind = kind;
    for (unsigned i = 0; i < 32; ++i) c->source_fingerprint[i] = kind ? (uint8_t)(i + variant) : 0;
}
int main(int argc, char **argv) {
    CHECK(argc == 5 || argc == 6);
    unsigned from = (unsigned)strtoul(argv[1], NULL, 10);
    unsigned to = (unsigned)strtoul(argv[2], NULL, 10);
    unsigned variant = (unsigned)strtoul(argv[3], NULL, 10);
    unsigned required = (unsigned)strtoul(argv[4], NULL, 10);
    apta_context_config_t cc; apta_context_config_init(&cc);
    cc.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    apta_context_t *ctx = NULL; CHECK(apta_context_create(&cc, &ctx) == 0);
    apta_session_config_t c; apta_session_config_init(&c);
    c.source_sample_rate = 8000; c.channel_count = 1;
    c.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    c.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    c.total_frames = argc == 6 ? APTA_TOTAL_FRAMES_UNKNOWN : 512; c.overview_frames_per_column = 256;
    c.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    identity(&c, from, 0);
    apta_session_t *writer = NULL; CHECK(apta_session_create(ctx, &c, &writer) == 0);
    push(writer, 0, 12000);
    const apta_result_t *seed = apta_session_acquire_result(writer); CHECK(seed);
    CHECK(apta_session_destroy(writer) == 0);
    identity(&c, to, variant);
    c.total_frames = 512;
    c.flags = required ? APTA_SESSION_FLAG_REQUIRE_SOURCE_IDENTITY_FOR_SEEDING : 0;
    apta_session_t *s = NULL; CHECK(apta_session_create(ctx, &c, &s) == 0);
    int status = apta_session_seed_from_result(s, seed);
    printf("%d\n", status);
    apta_result_release(seed);
    if (status == 0) {
        push(s, 256, -6000);
        CHECK(apta_session_signal_end_of_input(s, 512) == 0);
        apta_work_budget_t budget; apta_work_budget_init(&budget);
        CHECK(apta_session_process(s, &budget, NULL) == APTA_STATUS_END_OF_INPUT);
        const apta_result_t *r = apta_session_acquire_result(s); CHECK(r);
        uint64_t size = 0; CHECK(apta_result_query_serialized_size(r, NULL, &size) == 0);
        void *bytes = malloc((size_t)size); CHECK(bytes);
        size_t written = 0; CHECK(apta_result_serialize(r, NULL, bytes, (size_t)size, &written) == 0);
        CHECK(fwrite(bytes, 1, written, stdout) == written);
        free(bytes); apta_result_release(r);
    }
    CHECK(apta_session_destroy(s) == 0);
    CHECK(apta_context_destroy(ctx) == 0);
    return 0;
}
