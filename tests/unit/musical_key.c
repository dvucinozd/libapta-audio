// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/key/apta_key_internal.h"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #condition);                         \
            return EXIT_FAILURE;                                             \
        }                                                                    \
    } while (0)

static const float major_profile[12] = {
    6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f,
    2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f
};

static const float minor_profile[12] = {
    6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f,
    2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f
};

static void rotate_profile(
    float out[12],
    const float profile[12],
    uint32_t tonic)
{
    uint32_t pitch;
    for (pitch = 0u; pitch < 12u; ++pitch) {
        out[pitch] = profile[(pitch + 12u - tonic) % 12u];
    }
}

int main(void)
{
    float chroma[12];
    apta_key_candidate_t candidates[APTA_INTERNAL_KEY_CANDIDATE_COUNT];
    apta_key_view_t view;
    apta_status_t status;

    memcpy(chroma, major_profile, sizeof(chroma));
    status = apta_internal_key_select_chroma(
        chroma, 4u, candidates, &view);
    CHECK(status == APTA_STATUS_OK);
    CHECK(view.tonic == 0u);
    CHECK(view.mode == APTA_KEY_MODE_MAJOR);
    CHECK(view.state == APTA_FEATURE_STABLE);
    CHECK(view.candidate_count == APTA_INTERNAL_KEY_CANDIDATE_COUNT);
    CHECK(candidates[0].score >= candidates[1].score);
    CHECK(candidates[1].score >= candidates[2].score);

    rotate_profile(chroma, minor_profile, 9u);
    status = apta_internal_key_select_chroma(
        chroma, 1u, candidates, &view);
    CHECK(status == APTA_STATUS_OK);
    CHECK(view.tonic == 9u);
    CHECK(view.mode == APTA_KEY_MODE_MINOR);
    CHECK(view.state == APTA_FEATURE_PROVISIONAL);

    memset(chroma, 0, sizeof(chroma));
    status = apta_internal_key_select_chroma(
        chroma, 4u, candidates, &view);
    CHECK(status == APTA_STATUS_NOT_AVAILABLE);

    return EXIT_SUCCESS;
}
