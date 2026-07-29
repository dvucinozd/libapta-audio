// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_TEST_ALIGNMENT_H
#define APTA_TEST_ALIGNMENT_H

#include <stddef.h>

#if defined(_MSC_VER)
typedef union {
    long double long_double_value;
    long long long_long_value;
    void *pointer_value;
} apta_test_max_align_t;
#else
typedef max_align_t apta_test_max_align_t;
#endif

#endif /* APTA_TEST_ALIGNMENT_H */
