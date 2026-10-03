// SPDX-License-Identifier: Apache-2.0
// Test-only unchanged-C selective stream parser. Reads container on stdin.
#include <apta/apta.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) return 2; } while (0)
typedef struct { const uint8_t *bytes; size_t size; } source_t;
static apta_status_t APTA_CALL size_cb(void *user, uint64_t *out) {
    *out = ((source_t *)user)->size; return APTA_STATUS_OK;
}
static apta_status_t APTA_CALL read_cb(void *user, uint64_t offset, void *out,
                                     uint64_t requested, uint64_t *read) {
    source_t *source = user;
    if (offset > source->size) return APTA_ERROR_SOURCE;
    uint64_t n = source->size - offset;
    if (n > requested) n = requested;
    if (n > 3) n = 3;
    memcpy(out, source->bytes + (size_t)offset, (size_t)n);
    *read = n; return APTA_STATUS_OK;
}
int main(int argc, char **argv) {
    CHECK(argc == 2);
    uint8_t bytes[1048576], scratch[256];
    size_t n = fread(bytes, 1, sizeof(bytes), stdin);
    CHECK(n > 0 && n < sizeof(bytes) && !ferror(stdin));
    source_t source = {bytes,n};
    apta_context_config_t cc; apta_context_config_init(&cc);
    apta_context_t *context = NULL;
    CHECK(apta_context_create(&cc, &context) == APTA_STATUS_OK);
    apta_input_stream_t input; apta_input_stream_init(&input);
    input.user_data = &source; input.read_at = read_cb; input.get_size = size_cb;
    apta_stream_parse_options_t options; apta_stream_parse_options_init(&options);
    options.flags = APTA_PARSE_STRICT;
    options.requested_features = strtoull(argv[1], NULL, 0);
    options.scratch_buffer = scratch; options.scratch_buffer_size = sizeof(scratch);
    options.maximum_scratch_bytes = sizeof(scratch);
    const apta_result_t *result = NULL;
    apta_status_t status = apta_result_parse_from_stream(context, &options, &input, &result);
    if (status != APTA_STATUS_OK) {
        fprintf(stderr, "C stream status %d\n", status);
        CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
        return 3;
    }
    printf("%" PRIu64 "\n", apta_result_get_available_features(result));
    apta_result_release(result);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
