// SPDX-License-Identifier: Apache-2.0
/*
 * The global estimator may reorder the local estimator's candidates. It may
 * never invent one.
 *
 * That distinction is the whole design. An earlier attempt let S6 rescale S4's
 * winner by a metrical ratio, which could produce a tempo neither engine had
 * proposed -- 240.02 against a truth of 120.00 -- and lost more tracks than it
 * gained. Promotion selects from the list S4 already published, so the worst it
 * can do is pick a worse member of that list.
 *
 * This runs the same audio with and without the global grid requested and
 * requires the candidate values to be the same set either way.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

#define SAMPLE_RATE 44100u
#define BLOCK_FRAMES 4096u
#define TOTAL_FRAMES (SAMPLE_RATE * 20u)
#define BEAT_FRAMES 20672u
#define MAX_CANDIDATES 8u

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                   \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

typedef struct {
    uint32_t count;
    uint32_t tempo[MAX_CANDIDATES];
    uint32_t selected;
} outcome_t;

static int run(apta_context_t *context,
               apta_feature_mask_t features,
               const int16_t *audio,
               outcome_t *out)
{
    apta_session_config_t config;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_work_budget_t budget;
    apta_tempo_view_t tempo;
    uint32_t first = 0u;
    uint32_t index;
    apta_status_t status;

    memset(out, 0, sizeof(*out));

    apta_session_config_init(&config);
    config.source_sample_rate = SAMPLE_RATE;
    config.channel_count = 1u;
    config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    config.total_frames = TOTAL_FRAMES;
    config.requested_features = features;
    if (apta_session_create(context, &config, &session) != APTA_STATUS_OK) {
        return 1;
    }

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = BLOCK_FRAMES;
    budget.maximum_steps = 64u;
    while (first < TOTAL_FRAMES) {
        apta_pcm_block_t block;
        uint32_t count = TOTAL_FRAMES - first;
        uint32_t accepted = 0u;

        if (count > BLOCK_FRAMES) {
            count = BLOCK_FRAMES;
        }
        apta_pcm_block_init(&block);
        block.data = &audio[first];
        block.first_frame = first;
        block.frame_count = count;
        if (apta_session_push_pcm(session, &block, &accepted) !=
                APTA_STATUS_OK ||
            accepted != count) {
            return 1;
        }
        first += count;
        if (apta_session_process(session, &budget, NULL) < 0) {
            return 1;
        }
    }
    if (apta_session_signal_end_of_input(session, TOTAL_FRAMES) !=
        APTA_STATUS_OK) {
        return 1;
    }
    do {
        status = apta_session_process(session, &budget, NULL);
    } while (status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);
    if (status != APTA_STATUS_END_OF_INPUT) {
        return 1;
    }

    result = apta_session_acquire_result(session);
    if (result == NULL) {
        return 1;
    }
    apta_tempo_view_init(&tempo);
    if (apta_result_get_tempo(result, NULL, &tempo) != APTA_STATUS_OK) {
        apta_result_release(result);
        return 1;
    }
    out->selected = tempo.selected.tempo_millibpm;
    out->count = tempo.candidate_count < MAX_CANDIDATES
                     ? tempo.candidate_count
                     : MAX_CANDIDATES;
    for (index = 0u; index < out->count; ++index) {
        out->tempo[index] = tempo.candidates[index].tempo_millibpm;
    }
    apta_result_release(result);
    (void)apta_session_destroy(session);
    return 0;
}

static int contains(const outcome_t *set, uint32_t value)
{
    uint32_t index;

    for (index = 0u; index < set->count; ++index) {
        if (set->tempo[index] == value) {
            return 1;
        }
    }
    return 0;
}

int main(void)
{
    const apta_feature_mask_t local =
        APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_BPM |
        APTA_FEATURE_LOCAL_BEATGRID | APTA_FEATURE_CONFIDENCE;
    const apta_feature_mask_t with_global =
        local | APTA_FEATURE_GLOBAL_BEATGRID;
    apta_context_config_t context_config;
    apta_context_t *context = NULL;
    int16_t *audio;
    outcome_t alone;
    outcome_t endorsed;
    uint32_t index;

    audio = (int16_t *)malloc((size_t)TOTAL_FRAMES * sizeof(*audio));
    CHECK(audio != NULL);
    for (index = 0u; index < TOTAL_FRAMES; ++index) {
        audio[index] = (index % BEAT_FRAMES) < 160u ? (int16_t)28000 : 0;
    }

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = with_global;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    CHECK(run(context, local, audio, &alone) == 0);
    CHECK(run(context, with_global, audio, &endorsed) == 0);

    CHECK(alone.count > 0u);
    CHECK(endorsed.count == alone.count);

    /* The set is the same either way: promotion reorders, it does not add,
     * remove or compute. */
    for (index = 0u; index < endorsed.count; ++index) {
        if (!contains(&alone, endorsed.tempo[index])) {
            fprintf(stderr,
                    "candidate %u millibpm appears only when the global grid "
                    "is requested.\nPromotion must select from the list the "
                    "local estimator already produced.\n",
                    endorsed.tempo[index]);
            return 1;
        }
    }
    for (index = 0u; index < alone.count; ++index) {
        CHECK(contains(&endorsed, alone.tempo[index]));
    }

    /* And the published tempo is always the head of the list a host reads. */
    CHECK(alone.selected == alone.tempo[0]);
    CHECK(endorsed.selected == endorsed.tempo[0]);
    CHECK(contains(&alone, endorsed.selected));

    free(audio);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
