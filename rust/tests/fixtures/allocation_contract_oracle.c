// SPDX-License-Identifier: Apache-2.0
// Public reference contracts only: not an implementation of Rust C ownership.
#include <apta/apta.h>
#include <pthread.h>
#include <stdint.h>
#include <stdatomic.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(2); } } while (0)
#define FEATURES (APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_WAVEFORM_DETAIL | APTA_FEATURE_WAVEFORM_3BAND | APTA_FEATURE_BPM | APTA_FEATURE_LOCAL_BEATGRID | APTA_FEATURE_GLOBAL_BEATGRID | APTA_FEATURE_DYNAMIC_TEMPO | APTA_FEATURE_GRID_LOCKING | APTA_FEATURE_MUSICAL_KEY | APTA_FEATURE_METER_DOWNBEAT | APTA_FEATURE_CALIBRATED_QUALITY)
typedef struct { pthread_mutex_t mutex; unsigned calls, fail, live, classes, frees; size_t sizes[64], alignments[64]; unsigned flags[64]; } state_t;
static void *APTA_CALL allocate(void *user, size_t size, size_t alignment, apta_memory_flags_t flags) {
    state_t *s = user; CHECK(pthread_mutex_lock(&s->mutex) == 0); CHECK(size && alignment && !(alignment & (alignment-1)));
    CHECK(!(flags & ~(APTA_MEMORY_FAST | APTA_MEMORY_LARGE | APTA_MEMORY_PERSISTENT | APTA_MEMORY_TEMPORARY | APTA_MEMORY_DMA)));
    ++s->calls; s->classes |= flags;
    if (s->calls <= 64) { s->sizes[s->calls-1] = size; s->alignments[s->calls-1] = alignment; s->flags[s->calls-1] = flags; }
    if (s->calls == s->fail) { CHECK(pthread_mutex_unlock(&s->mutex) == 0); return NULL; }
    // malloc meets the reference's requested fundamental alignments.
    CHECK(alignment <= _Alignof(max_align_t));
    void *p = malloc(size); CHECK(p); ++s->live; CHECK(pthread_mutex_unlock(&s->mutex) == 0); return p;
}
static void APTA_CALL release(void *user, void *p) {
    state_t *s = user; CHECK(pthread_mutex_lock(&s->mutex) == 0); CHECK(p && s->live); --s->live; ++s->frees; free(p); CHECK(pthread_mutex_unlock(&s->mutex) == 0);
}
static void configure(apta_context_config_t *cc, apta_session_config_t *sc, state_t *state) {
    apta_context_config_init(cc); cc->requested_capabilities = FEATURES;
    cc->allocator.user_data = state; cc->allocator.allocate = allocate; cc->allocator.deallocate = release;
    apta_session_config_init(sc); sc->requested_features = FEATURES; sc->source_sample_rate = 8000;
    sc->channel_count = 1; sc->channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    sc->sample_format = APTA_SAMPLE_F32_NATIVE_INTERLEAVED; sc->total_frames = 8192;
    sc->overview_frames_per_column = 64;
}
static void *reader(void *p) {
    const apta_result_t *r = p;
    apta_result_info_t info; apta_result_info_init(&info); CHECK(apta_result_get_info(r, &info) == 0);
    CHECK(info.generation > 0); apta_result_release(r); return NULL;
}
static unsigned exercise(unsigned failure) {
    state_t state = {.mutex = PTHREAD_MUTEX_INITIALIZER}; state.fail = failure;
    apta_context_config_t cc; apta_session_config_t sc; configure(&cc, &sc, &state);
    apta_context_t *c = NULL; apta_session_t *s = NULL;
    int status = apta_context_create(&cc, &c);
    if (status == 0) {
        status = apta_session_create(c, &sc, &s);
        if (status == 0) {
            float pcm[4096] = {0}; pcm[0] = 0.75f;
            apta_work_budget_t budget; apta_work_budget_init(&budget);
            for (unsigned first = 0; first < 8192 && status >= 0; first += 4096) {
                apta_pcm_block_t b; apta_pcm_block_init(&b); b.first_frame = first; b.frame_count = 4096; b.data = pcm;
                uint32_t accepted = 0; status = apta_session_push_pcm(s, &b, &accepted);
                if (status == 0) { CHECK(accepted == 4096); status = apta_session_process(s, &budget, NULL); }
            }
            if (status >= 0) { status = apta_session_signal_end_of_input(s, 8192); }
            if (status >= 0) { status = apta_session_process(s, &budget, NULL); }
            CHECK(apta_context_destroy(c) == APTA_ERROR_BUSY);
            const apta_result_t *r = apta_session_acquire_result(s); CHECK(r);
            CHECK(apta_session_destroy(s) == 0);
            CHECK(apta_context_destroy(c) == APTA_ERROR_BUSY);
            pthread_t thread; CHECK(pthread_create(&thread, NULL, reader, (void *)r) == 0);
            CHECK(pthread_join(thread, NULL) == 0);
        } else { CHECK(s == NULL); }
        CHECK(apta_context_destroy(c) == 0);
    } else { CHECK(c == NULL); }
    CHECK(state.live == 0);
    if (failure) { CHECK(state.calls >= failure); printf("failure %u %d\n", failure, status); CHECK(status == APTA_ERROR_OUT_OF_MEMORY || status == APTA_STATUS_END_OF_INPUT); }
    else { CHECK(status == APTA_STATUS_END_OF_INPUT); printf("classes %u\n", state.classes);
        for (unsigned i = 0; i < state.calls && i < 64; ++i) printf("allocation %u %zu %zu %u\n", i+1, state.sizes[i], state.alignments[i], state.flags[i]); }
    CHECK(pthread_mutex_destroy(&state.mutex) == 0);
    return state.calls;
}
typedef struct { apta_session_t *session; atomic_uint stop; atomic_uint reads; } readers_t;
static void *concurrent_reader(void *user) {
    readers_t *state = user; uint64_t generation = 0;
    do {
        const apta_result_t *r = apta_session_acquire_result(state->session); CHECK(r);
        apta_result_info_t info; apta_result_info_init(&info); CHECK(apta_result_get_info(r, &info) == 0);
        CHECK(info.generation >= generation); generation = info.generation;
        sched_yield();
        apta_result_info_t again; apta_result_info_init(&again); CHECK(apta_result_get_info(r, &again) == 0);
        CHECK(again.generation == info.generation && again.available_features == info.available_features);
        atomic_fetch_add(&state->reads, 1); apta_result_release(r);
    } while (!atomic_load(&state->stop));
    return NULL;
}
static void concurrent_publication(void) {
    state_t state = {.mutex = PTHREAD_MUTEX_INITIALIZER};
    apta_context_config_t cc; apta_session_config_t sc; configure(&cc, &sc, &state);
    apta_context_t *c = NULL; apta_session_t *s = NULL; CHECK(apta_context_create(&cc, &c) == 0);
    CHECK(apta_session_create(c, &sc, &s) == 0);
    readers_t readers = {.session = s}; atomic_init(&readers.stop, 0); atomic_init(&readers.reads, 0);
    // One processing owner plus one reader: concurrency remains two.
    pthread_t thread; CHECK(pthread_create(&thread, NULL, concurrent_reader, &readers) == 0);
    apta_work_budget_t budget; apta_work_budget_init(&budget);
    for (unsigned first = 0; first < 8192; first += 64) {
        float pcm[64]; for (unsigned i = 0; i < 64; ++i) pcm[i] = (float)i / 128;
        apta_pcm_block_t b; apta_pcm_block_init(&b); b.first_frame = first; b.frame_count = 64; b.data = pcm;
        uint32_t accepted = 0; CHECK(apta_session_push_pcm(s, &b, &accepted) == 0 && accepted == 64);
        CHECK(apta_session_process(s, &budget, NULL) >= 0);
        unsigned before = atomic_load(&readers.reads);
        while (atomic_load(&readers.reads) == before) sched_yield();
    }
    atomic_store(&readers.stop, 1); CHECK(pthread_join(thread, NULL) == 0); CHECK(atomic_load(&readers.reads) > 0);
    CHECK(apta_session_destroy(s) == 0); CHECK(apta_context_destroy(c) == 0 && state.live == 0);
    CHECK(pthread_mutex_destroy(&state.mutex) == 0);
    puts("concurrent custom_allocator publication monotonic_readers released");
}
int main(void) {
    unsigned calls = exercise(0);
    for (unsigned i = 1; i <= calls; ++i) exercise(i);
    printf("failure_points %u\n", calls);
    state_t state = {.mutex = PTHREAD_MUTEX_INITIALIZER}; apta_context_config_t cc; apta_session_config_t sc; configure(&cc, &sc, &state);
    apta_context_t *c = NULL;
    cc.allocator.deallocate = NULL; CHECK(apta_context_create(&cc, &c) == APTA_ERROR_INVALID_ARGUMENT && !c && !state.calls);
    configure(&cc, &sc, &state); CHECK(apta_context_create(&cc, &c) == 0);
    sc.flags = APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS;
    apta_memory_requirements_t req; apta_memory_requirements_init(&req);
    CHECK(apta_query_workspace_requirements(&sc, &req) == 0);
    CHECK(req.minimum_bytes && req.recommended_bytes >= req.minimum_bytes);
    size_t size = (req.minimum_bytes + req.required_alignment - 1) & ~(req.required_alignment-1);
    void *workspace = aligned_alloc(req.required_alignment, size); CHECK(workspace);
    sc.static_workspace = workspace; sc.static_workspace_size = req.minimum_bytes - 1;
    apta_session_t *s = NULL; unsigned before = state.calls;
    CHECK(apta_session_create(c, &sc, &s) < 0 && !s && state.calls == before);
    sc.static_workspace_size = req.minimum_bytes;
    CHECK(apta_session_create(c, &sc, &s) == 0);
    CHECK(apta_session_destroy(s) == 0); free(workspace);
    CHECK(apta_context_destroy(c) == 0 && state.live == 0);
    printf("workspace_layout %zu %zu %zu\n", req.minimum_bytes, req.recommended_bytes, req.required_alignment);
    CHECK(pthread_mutex_destroy(&state.mutex) == 0);
    concurrent_publication();
    puts("workspace exact_minimum; invalid_allocator before_allocation; retained_release other_thread");
    return 0;
}
