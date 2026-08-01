// SPDX-License-Identifier: Apache-2.0
/* Per-feature CPU cost of apta_session_process(). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <apta/apta.h>

#define BLOCK_FRAMES 1024u
#define WS_BYTES (4u * 1024u * 1024u)

static int16_t g_pcm[BLOCK_FRAMES * 2u];
static uint64_t g_total_frames;
static uint32_t g_beat_frames;
static void *g_ws;

static void *ha(void *u, size_t s, size_t a, apta_memory_flags_t f)
{ (void)u; (void)f; (void)a; return malloc(s); }
static void hf(void *u, void *p) { (void)u; free(p); }
static void *hr(void *u, void *p, size_t n, size_t a, apta_memory_flags_t f)
{ (void)u; (void)f; (void)a; return realloc(p, n); }
static uint64_t hc(void *u) { (void)u; return 0u; }

static apta_status_t sr(void *u, apta_source_frame_t first,
                        uint32_t req, apta_pcm_block_t *out)
{
    uint32_t i, n;
    (void)u;
    if (first >= g_total_frames) return APTA_STATUS_END_OF_INPUT;
    n = req > BLOCK_FRAMES ? BLOCK_FRAMES : req;
    if (first + n > g_total_frames) n = (uint32_t)(g_total_frames - first);
    for (i = 0u; i < n; ++i) {
        uint64_t ph = (first + i) % g_beat_frames;
        int16_t v = ph < 160u ? (int16_t)28000 : (int16_t)0;
        g_pcm[i * 2u] = v; g_pcm[i * 2u + 1u] = v;
    }
    apta_pcm_block_init(out);
    out->data = g_pcm; out->first_frame = first; out->frame_count = n;
    return APTA_STATUS_OK;
}
static void srl(void *u, apta_pcm_block_t *b) { (void)u; (void)b; }
static uint64_t stf(void *u) { (void)u; return g_total_frames; }

static void measure(const char *name, apta_feature_mask_t features,
                    uint32_t rate, uint32_t seconds)
{
    apta_context_config_t cc; apta_session_config_t sc;
    apta_pcm_source_t src; apta_work_budget_t bud;
    apta_context_t *ctx = NULL; apta_session_t *s = NULL;
    apta_status_t st; unsigned long calls = 0ul;
    clock_t t0, t1; double total_ms;

    g_total_frames = (uint64_t)rate * seconds;
    g_beat_frames = (uint32_t)((uint64_t)rate * 60u / 128u);  /* 128 BPM */

    apta_context_config_init(&cc);
    cc.allocator.allocate = ha; cc.allocator.deallocate = hf;
    cc.allocator.reallocate = hr; cc.clock.monotonic_time_ns = hc;
    cc.requested_capabilities = features;
    if (apta_context_create(&cc, &ctx) < 0) { printf("%-34s ctx fail\n", name); return; }

    memset(g_ws, 0, WS_BYTES);
    apta_session_config_init(&sc);
    sc.input_mode = APTA_INPUT_MODE_PULL;
    sc.source_sample_rate = rate; sc.channel_count = 2u;
    sc.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    sc.channel_layout = APTA_CHANNEL_LAYOUT_STEREO;
    sc.total_frames = g_total_frames;
    sc.requested_features = features;
    sc.static_workspace = g_ws; sc.static_workspace_size = WS_BYTES;
    sc.flags = APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS;
    if (apta_session_create(ctx, &sc, &s) < 0) {
        printf("%-34s session fail\n", name);
        (void)apta_context_destroy(ctx); return;
    }
    apta_pcm_source_init(&src);
    src.read_frames = sr; src.release_frames = srl; src.get_total_frames = stf;
    if (apta_session_set_source(s, &src) < 0) {
        printf("%-34s source fail\n", name);
        (void)apta_session_destroy(s); (void)apta_context_destroy(ctx); return;
    }
    apta_work_budget_init(&bud);
    bud.maximum_input_frames = BLOCK_FRAMES;
    bud.maximum_steps = 16u;

    t0 = clock();
    for (;;) {
        st = apta_session_process(s, &bud, NULL);
        calls += 1ul;
        if (st == APTA_STATUS_END_OF_INPUT || st < 0) break;
        if (calls > 200000ul) break;
    }
    t1 = clock();
    total_ms = 1000.0 * (double)(t1 - t0) / (double)CLOCKS_PER_SEC;

    printf("%-34s calls=%6lu total=%9.1f ms  per_call=%9.1f us  x_rt=%6.1f\n",
           name, calls, total_ms,
           calls ? total_ms * 1000.0 / (double)calls : 0.0,
           total_ms > 0.0 ? (double)seconds * 1000.0 / total_ms : 0.0);

    (void)apta_session_destroy(s);
    (void)apta_context_destroy(ctx);
}

int main(void)
{
    const apta_feature_mask_t ov  = APTA_FEATURE_WAVEFORM_OVERVIEW;
    const apta_feature_mask_t ovc = ov | APTA_FEATURE_CONFIDENCE;
    const apta_feature_mask_t bpm = ovc | APTA_FEATURE_BPM;
    const apta_feature_mask_t loc = bpm | APTA_FEATURE_LOCAL_BEATGRID;
    const apta_feature_mask_t glo = loc | APTA_FEATURE_GLOBAL_BEATGRID;
    const apta_feature_mask_t dyn = glo | APTA_FEATURE_DYNAMIC_TEMPO;
    const apta_feature_mask_t all = dyn | APTA_FEATURE_WAVEFORM_DETAIL |
                                    APTA_FEATURE_GRID_LOCKING;

    setvbuf(stdout, NULL, _IONBF, 0);
    g_ws = malloc(WS_BYTES);
    if (!g_ws) { puts("alloc failed"); return 1; }

    puts("5 min @ 44.1 kHz, 1024-frame blocks");
    measure("overview",               ov,  44100u, 300u);
    measure("overview+confidence",    ovc, 44100u, 300u);
    measure("+BPM",                   bpm, 44100u, 300u);
    measure("+local grid",            loc, 44100u, 300u);
    measure("+global grid",           glo, 44100u, 300u);
    measure("+dynamic tempo",         dyn, 44100u, 300u);
    measure("+detail+locking (full)", all, 44100u, 300u);

    free(g_ws);
    return 0;
}
