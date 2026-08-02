// SPDX-License-Identifier: Apache-2.0
/*
 * Every section a container can carry must be one the tools can name.
 *
 * `apta-inspect` kept a hand-written list of section codes it would accept for
 * `--section` and print in its output. A section the writer could emit but the
 * list did not mention was invisible: the file held it, the reader restored it,
 * and the tool reported nothing. GGRD and REVN were both in that position, and
 * the same shape of mistake had already been made three times with the feature
 * mask.
 *
 * So this does not check a list against another list. It builds a result with
 * every feature requested, serializes it, and reads the section directory out
 * of the bytes. Whatever the writer actually put in the file is the authority.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

#include "../../tools/apta_tool_common.h"

#define SAMPLE_RATE 44100u
#define BLOCK_FRAMES 4096u
#define TOTAL_FRAMES (SAMPLE_RATE * 12u)
#define BEAT_FRAMES 20672u

/* Container layout, from the serializer. */
#define CONTAINER_SECTION_COUNT_OFFSET 20u
#define CONTAINER_DIRECTORY_OFFSET 96u
#define CONTAINER_DIRECTORY_ENTRY_SIZE 40u

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                   \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static uint32_t get_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

int main(void)
{
    const apta_feature_mask_t features = apta_tool_all_features();
    apta_context_config_t context_config;
    apta_context_t *context = NULL;
    apta_session_config_t config;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_work_budget_t budget;
    apta_serialize_options_t options;
    static int16_t samples[BLOCK_FRAMES * 2u];
    uint8_t *container = NULL;
    size_t container_size = 0u;
    uint64_t written = 0u;
    uint32_t first = 0u;
    uint32_t section_count;
    uint32_t index;
    int unknown = 0;
    apta_status_t status;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = features;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&config);
    config.source_sample_rate = SAMPLE_RATE;
    config.channel_count = 2u;
    config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    config.channel_layout = APTA_CHANNEL_LAYOUT_STEREO;
    config.total_frames = TOTAL_FRAMES;
    config.requested_features = features;
    CHECK(apta_session_create(context, &config, &session) == APTA_STATUS_OK);

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = BLOCK_FRAMES;
    budget.maximum_steps = 64u;

    /* A click train, so the tempo and grid stages have something to publish
     * and their sections are actually written. */
    while (first < TOTAL_FRAMES) {
        apta_pcm_block_t block;
        uint32_t count = TOTAL_FRAMES - first;
        uint32_t i;

        if (count > BLOCK_FRAMES) {
            count = BLOCK_FRAMES;
        }
        for (i = 0u; i < count; ++i) {
            const int16_t value =
                ((first + i) % BEAT_FRAMES) < 160u ? (int16_t)28000 : 0;
            samples[i * 2u] = value;
            samples[i * 2u + 1u] = value;
        }
        apta_pcm_block_init(&block);
        block.data = samples;
        block.first_frame = first;
        block.frame_count = count;
        {
            uint32_t accepted = 0u;
            CHECK(apta_session_push_pcm(session, &block, &accepted) ==
                  APTA_STATUS_OK);
            CHECK(accepted == count);
        }
        status = apta_session_process(session, &budget, NULL);
        CHECK(status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);
        first += count;
    }
    CHECK(apta_session_signal_end_of_input(session, TOTAL_FRAMES) ==
          APTA_STATUS_OK);
    do {
        status = apta_session_process(session, &budget, NULL);
    } while (status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);
    CHECK(status == APTA_STATUS_END_OF_INPUT);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);

    apta_serialize_options_init(&options);
    CHECK(apta_result_query_serialized_size(result, &options, &written) ==
          APTA_STATUS_OK);
    CHECK(written > CONTAINER_DIRECTORY_OFFSET);
    container_size = (size_t)written;
    container = (uint8_t *)malloc(container_size);
    CHECK(container != NULL);
    CHECK(apta_result_serialize(result, &options, container, container_size,
                                &written) == APTA_STATUS_OK);

    section_count = get_u32(container + CONTAINER_SECTION_COUNT_OFFSET);
    CHECK(section_count > 0u);
    printf("container carries %u section(s):", section_count);
    for (index = 0u; index < section_count; ++index) {
        const uint8_t *entry = container +
                               CONTAINER_DIRECTORY_OFFSET +
                               (size_t)index * CONTAINER_DIRECTORY_ENTRY_SIZE;
        char code[5];

        CHECK((size_t)(entry - container) + CONTAINER_DIRECTORY_ENTRY_SIZE <=
              container_size);
        memcpy(code, entry, 4u);
        code[4] = '\0';
        printf(" %s", code);
        if (!apta_tool_section_is_known(code)) {
            fprintf(stderr,
                    "\nsection \"%s\" is in the container but not in\n"
                    "apta_tool_section_codes, so apta-inspect can neither\n"
                    "select nor display it.\n",
                    code);
            unknown = 1;
        }
    }
    printf("\n");

    free(container);
    apta_result_release(result);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return unknown;
}
