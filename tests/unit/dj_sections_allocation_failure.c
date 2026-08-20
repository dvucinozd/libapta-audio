// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <apta/apta.h>

#define FILE_SIZE 664u

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

typedef struct {
    uint32_t allocation_call;
    uint32_t fail_at_call;
    uint32_t outstanding;
} allocator_state_t;

static void *APTA_CALL test_allocate(
    void *user_data, size_t size, size_t alignment, apta_memory_flags_t flags)
{
    allocator_state_t *state = (allocator_state_t *)user_data;
    void *memory;
    (void)alignment;
    (void)flags;
    ++state->allocation_call;
    if (state->fail_at_call == state->allocation_call) return NULL;
    memory = malloc(size);
    if (memory != NULL) ++state->outstanding;
    return memory;
}

static void APTA_CALL test_deallocate(void *user_data, void *memory)
{
    allocator_state_t *state = (allocator_state_t *)user_data;
    if (memory != NULL) {
        free(memory);
        --state->outstanding;
    }
}

static int load_fixture(uint8_t bytes[FILE_SIZE])
{
    FILE *file = fopen(APTA_DJ_GOLDEN_HEX_PATH, "rb");
    int high = -1;
    size_t size = 0u;
    int character;
    if (file == NULL) return 0;
    while ((character = fgetc(file)) != EOF) {
        int value;
        if (character >= '0' && character <= '9') value = character - '0';
        else if (character >= 'a' && character <= 'f') value = character - 'a' + 10;
        else if (character >= 'A' && character <= 'F') value = character - 'A' + 10;
        else continue;
        if (high < 0) high = value;
        else {
            if (size == FILE_SIZE) { fclose(file); return 0; }
            bytes[size++] = (uint8_t)((high << 4) | value);
            high = -1;
        }
    }
    fclose(file);
    return high < 0 && size == FILE_SIZE;
}

static int run_case(
    const uint8_t bytes[FILE_SIZE], uint32_t fail_at, uint32_t *calls_out)
{
    allocator_state_t state = {0u, fail_at, 0u};
    apta_context_config_t config;
    apta_context_t *context = NULL;
    const apta_result_t *result = NULL;
    apta_key_view_t key;
    apta_meter_view_t meter;
    apta_quality_view_t quality;
    apta_status_t status;
    int ok = 0;

    apta_context_config_init(&config);
    config.allocator.user_data = &state;
    config.allocator.allocate = test_allocate;
    config.allocator.deallocate = test_deallocate;
    if (apta_context_create(&config, &context) != APTA_STATUS_OK) goto cleanup;
    status = apta_result_parse(context, NULL, bytes, FILE_SIZE, &result);
    if (fail_at == 0u) {
        if (status != APTA_STATUS_OK || result == NULL) goto cleanup;
    } else {
        if (status != APTA_ERROR_OUT_OF_MEMORY || result != NULL) goto cleanup;
        /* A failed augmentation must leave the context reusable. */
        state.fail_at_call = 0u;
        status = apta_result_parse(context, NULL, bytes, FILE_SIZE, &result);
        if (status != APTA_STATUS_OK || result == NULL) goto cleanup;
    }
    apta_key_view_init(&key);
    apta_meter_view_init(&meter);
    apta_quality_view_init(&quality);
    if (apta_result_get_key(result, NULL, &key) != APTA_STATUS_OK ||
        key.candidate_count != 2u ||
        apta_result_get_meter(result, NULL, &meter) != APTA_STATUS_OK ||
        meter.segment_count != 2u ||
        apta_result_get_quality(result, APTA_FEATURE_MUSICAL_KEY, &quality) !=
            APTA_STATUS_OK) goto cleanup;
    ok = 1;

cleanup:
    if (calls_out != NULL) *calls_out = state.allocation_call;
    if (result != NULL) apta_result_release(result);
    if (context != NULL && apta_context_destroy(context) != APTA_STATUS_OK) {
        ok = 0;
    }
    return ok && state.outstanding == 0u;
}

int main(void)
{
    uint8_t bytes[FILE_SIZE];
    uint32_t total_calls = 0u;
    uint32_t fail_at;
    CHECK(load_fixture(bytes));
    CHECK(run_case(bytes, 0u, &total_calls));
    CHECK(total_calls >= 7u);
    for (fail_at = 2u; fail_at <= total_calls; ++fail_at) {
        CHECK(run_case(bytes, fail_at, NULL));
    }
    return 0;
}
