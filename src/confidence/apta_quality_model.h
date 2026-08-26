// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_QUALITY_MODEL_H
#define APTA_QUALITY_MODEL_H

#include "../core/apta_internal.h"

/* Accepted Task-6 BPM confidence calibration model
 * `isotonic-pav-clamped-v1`, protocol ID 1867860160. The lookup maps an
 * unchanged detector raw confidence to a calibrated value and is clamped so
 * integration can only lower a reported confidence, never raise it. The
 * privacy-safe holdout summary is retained under evidence/1.1. */
#define APTA_INTERNAL_BPM_QUALITY_MODEL_ID UINT32_C(1867860160)

apta_confidence_value_t apta_internal_bpm_quality_calibrate(
    apta_confidence_value_t raw_confidence);

uint16_t apta_internal_quality_coverage_permille(
    uint64_t covered,
    uint64_t total);

#endif /* APTA_QUALITY_MODEL_H */
