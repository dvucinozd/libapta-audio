// SPDX-License-Identifier: Apache-2.0
// Test-only unchanged-C parse/canonical-serialize oracle. Binary stdin/stdout.
#include <apta/apta.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) return 2; } while (0)
int main(void) {
    uint8_t input[1048576];
    size_t n = fread(input, 1, sizeof(input), stdin);
    CHECK(n > 0 && n < sizeof(input) && !ferror(stdin));
    apta_context_config_t cc; apta_context_config_init(&cc);
    apta_context_t *context = NULL;
    CHECK(apta_context_create(&cc, &context) == APTA_STATUS_OK);
    apta_parse_options_t po; apta_parse_options_init(&po);
    po.flags = APTA_PARSE_STRICT;
    const apta_result_t *result = NULL;
    CHECK(apta_result_parse(context, &po, input, n, &result) == APTA_STATUS_OK);
    uint64_t size = 0;
    CHECK(apta_result_query_serialized_size(result, NULL, &size) == APTA_STATUS_OK);
    CHECK(size <= sizeof(input));
    uint8_t *output = malloc((size_t)size);
    CHECK(output != NULL);
    size_t written = 0;
    CHECK(apta_result_serialize(result, NULL, output, (size_t)size, &written) == APTA_STATUS_OK);
    CHECK(written == size && fwrite(output, 1, written, stdout) == written);
    free(output);
    apta_result_release(result);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
