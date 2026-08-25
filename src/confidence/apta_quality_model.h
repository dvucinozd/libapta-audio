// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_QUALITY_MODEL_H
#define APTA_QUALITY_MODEL_H

#include "../core/apta_internal.h"

/* Accepted Task-6 tempo confidence calibration model
 * `isotonic-pav-clamped-v1`, protocol ID 1867860160.
 *
 * Frozen training evidence: 328 rows (historical Rekordbox corpus endorsed
 * run, Ballroom development partition, ASAP development partition, automated
 * DJ diagnostic corpus), one HEAD binary, production ensemble mode. Holdout
 * acceptance: Brier 0.179 -> 0.152, ECE 0.282 -> 0.198, high-confidence
 * errors preserved at zero. See
 * docs/status/APTA-1.1-CONFIDENCE-CALIBRATION-PROTOCOL.md.
 *
 * The lookup maps an unchanged detector raw confidence to a calibrated value.
 * The fitted isotonic curve is additionally clamped so integration can only
 * lower a reported confidence, never raise it. */
#define APTA_INTERNAL_BPM_QUALITY_MODEL_ID UINT32_C(1867860160)

apta_confidence_value_t apta_internal_bpm_quality_calibrate(
    apta_confidence_value_t raw_confidence);

#endif /* APTA_QUALITY_MODEL_H */
