#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generated reference-bracketing unit controls, without source data or I/O."""
from copy import deepcopy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from apta_reference_support import reference_support


class SupportTests(unittest.TestCase):
    def window(self, first=10, end=20):
        return {'first_frame': first, 'end_frame': end}

    def test_exact_endpoints_and_four_coverage_cases(self):
        for beats, complete in (([10, 19], 1), ([11, 19], 0), ([10, 18], 0), ([11, 18], 0)):
            with self.subTest(beats=beats):
                self.assertEqual(reference_support(beats, [self.window()]), [
                    {'windows': 1, 'reference_complete_windows': complete,
                     'reference_incomplete_windows': 1 - complete}])

    def test_single_beat_one_frame_and_unbounded_integer_domain(self):
        for frame in (0, 10, 1 << 100):
            self.assertEqual(reference_support([frame], [self.window(frame, frame + 1)])[0]['reference_complete_windows'], 1)

    def test_gaps_and_eight_windows_preserve_input(self):
        windows = [self.window(i * 10, i * 10 + 5) for i in range(8)]
        beats = [0, 74]
        before = deepcopy((beats, windows))
        result = reference_support(beats, windows)
        self.assertEqual(sum(row['reference_complete_windows'] for row in result), 8)
        self.assertEqual((beats, windows), before)

    def test_coverage_does_not_infer_density_or_candidate_success(self):
        self.assertEqual(reference_support([0, 1000], [self.window()])[0]['reference_complete_windows'], 1)

    def test_missing_empty_wrong_type_unsorted_duplicate_and_boolean_beats(self):
        for beats in (None, [], (), 'unknown', {}, [True], [False, 19], [10.0, 19], [-1, 19], [19, 10], [10, 10], [None]):
            with self.subTest(beats=beats), self.assertRaises(ValueError):
                reference_support(beats, [self.window()])

    def test_invalid_window_shapes_types_order_and_extents(self):
        invalid = (None, [], (), {}, [self.window()] * 9, [None],
                   [{'first_frame': 10}], [dict(self.window(), extra=1)],
                   [self.window(True, 20)], [self.window(10, 20.0)], [self.window(-1, 20)],
                   [self.window(20, 20)], [self.window(21, 20)],
                   [self.window(), self.window(19, 25)], [self.window(), self.window(0, 5)])
        for windows in invalid:
            with self.subTest(windows=windows), self.assertRaises(ValueError):
                reference_support([0, 100], windows)

    def test_generated_400_200_and_interface_only_401_199(self):
        windows = [self.window(i * 10, (i + 1) * 10) for i in range(6)]
        for extra in (False, True):
            results = []
            for index in range(100):
                results.extend(reference_support([10, 59 if extra and index == 0 else 49], windows))
            self.assertEqual(sum(row['windows'] for row in results), 600)
            self.assertEqual(sum(row['reference_complete_windows'] for row in results), 400 + int(extra))
            self.assertEqual(sum(row['reference_incomplete_windows'] for row in results), 200 - int(extra))


if __name__ == '__main__':
    unittest.main()
