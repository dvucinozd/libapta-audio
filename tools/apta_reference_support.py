#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Pure reference-bracketing metadata, available only to evaluation callers."""


def reference_support(reference_beats, windows):
    """Return exact complementary counters; unknown coverage is an error.

    The caller owns source/frame-domain validation. This routine deliberately
    makes no assumption about event density, candidate success or local gaps.
    Work is O(reference beats + windows), without sorting or mutating inputs.
    """
    if (type(reference_beats) is not list or not reference_beats
            or any(type(beat) is not int or beat < 0 for beat in reference_beats)
            or any(a >= b for a, b in zip(reference_beats, reference_beats[1:]))):
        raise ValueError('nonempty strictly increasing nonnegative integer reference beats required')
    if type(windows) is not list or not 1 <= len(windows) <= 8:
        raise ValueError('one to eight half-open windows required')
    result, previous_end = [], None
    for window in windows:
        if (type(window) is not dict or set(window) != {'first_frame', 'end_frame'}
                or any(type(window[key]) is not int or window[key] < 0 for key in window)):
            raise ValueError('exact nonnegative integer window geometry required')
        first, end = window['first_frame'], window['end_frame']
        if first >= end or previous_end is not None and first < previous_end:
            raise ValueError('ordered nonoverlapping half-open windows required')
        context = reference_beats[0] <= first and end - 1 <= reference_beats[-1]
        result.append({'windows': 1, 'reference_complete_windows': int(context),
                       'reference_incomplete_windows': 1 - int(context)})
        previous_end = end
    return result
