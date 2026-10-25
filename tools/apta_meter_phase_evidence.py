# SPDX-License-Identifier: Apache-2.0
"""MC-05 evaluation-only companion. Not imported by any campaign dispatcher."""

import apta_context_matched as canonical
import apta_meter_phase_points as geometry

REGISTRATION_SHA256 = geometry.REGISTRATION_SHA256


def evaluate_source(windows, truth, *, total_frames, sample_rate, known_gaps):
    """Preserve published evidence and evaluate every K point with frozen P3.

    References never enter geometry extraction or alter publication. Canonical
    validation and per-point arithmetic remain the sole scoring authority.
    """
    phases = geometry.extract_source(windows, total_frames=total_frames,
                                    sample_rate=sample_rate, known_gaps=known_gaps)
    canonical._truth(truth)
    results = []
    for window, phase in zip(windows, phases):
        meter, points = window['meter'], phase['grid_points']
        first, end = phase['first_frame'], phase['end_frame']
        published = canonical.meter_evidence(meter, points, first, end, truth, 'context')
        scores = []
        for point in phase['phase_points']:
            witness = dict(meter, downbeat_frame=point['position_q32'] >> 32,
                           downbeat_fraction_q32=point['position_q32'] & (geometry.Q - 1),
                           downbeat_ordinal=point['ordinal'])
            evidence = canonical.meter_evidence(witness, points, first, end, truth, 'context')
            scores.append({'point': dict(point), 'evidence': evidence})
        supported = sum(s['evidence']['downbeat_supported'] for s in scores)
        correct = sum(s['evidence']['score']['downbeat_correct'] for s in scores)
        results.append({'phase': phase, 'published': published,
                        'phase_point_count': len(scores), 'supported_point_count': supported,
                        'correct_point_count': correct, 'E_supported': int(supported > 0),
                        'all_correct': int(correct == supported) if supported else None,
                        'conditional_accuracy': {'numerator': correct, 'denominator': supported}
                        if supported else None, 'point_scores': scores})
    return results
