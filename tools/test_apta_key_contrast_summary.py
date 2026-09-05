# SPDX-License-Identifier: Apache-2.0
import copy
import math
from pathlib import Path
import tempfile
import unittest

import apta_key_contrast_summary as summary


def row(variants=1):
    raw = [[float(i + 1) for i in range(36)] for _ in range(variants)]
    compressed = [[math.log1p(v) for v in band] for band in raw]
    fold = lambda values: [sum(values[v][p + 12 * octave] / variants for v in range(variants) for octave in range(3))
                           for p in range(12)]
    return {'contrast': dict(variants=variants, observations=36 * variants,
                            native_cumulative_bit_identical=True, raw_energy_by_variant=raw,
                            compressed_by_variant=compressed, raw_folded=fold(raw),
                            compressed_window=fold(compressed))}


class ContrastTests(unittest.TestCase):
    def test_uniform_and_impulse_contrast(self):
        self.assertAlmostEqual(summary.contrast([1.] * 12)['min_over_mean'], 1)
        self.assertAlmostEqual(summary.contrast([1.] * 12)['normalized_entropy'], 1)
        self.assertEqual(summary.contrast([1.] + [0.] * 11)['normalized_entropy'], 0)
        self.assertEqual(summary.contrast([1.] + [0.] * 11)['min_over_mean'], 0)

    def test_profile_identities_all_24_keys(self):
        for mode in range(2):
            for tonic in range(12):
                v = [summary.PROFILES[mode][(p + 12 - tonic) % 12] for p in range(12)]
                scores = summary.scores(v)
                self.assertEqual(max(range(24), key=lambda i: scores[i]), mode * 12 + tonic)
                self.assertAlmostEqual(scores[mode * 12 + tonic], 1)

    def test_log_and_fold_for_both_builds(self):
        for variants in (1, 3):
            summary.inspect_trace(row(variants), variants)

    def test_nonfinite_empty_and_wrong_size(self):
        for values in ([0.] * 12, [1.] * 11, [float('nan')] * 12, [float('inf')] * 12, [-1.] * 12, [True] * 12):
            with self.subTest(values=values), self.assertRaises(ValueError):
                summary.vector(values, 12)

    def test_compression_or_folding_corruption_rejected(self):
        for field in ('raw_energy_by_variant', 'compressed_by_variant', 'raw_folded', 'compressed_window'):
            data = row()
            if isinstance(data['contrast'][field][0], list):
                data['contrast'][field][0][0] += 1
            else:
                data['contrast'][field][0] += 1
            with self.subTest(field=field), self.assertRaises(ValueError):
                summary.inspect_trace(data, 1)

    def test_incomplete_observation_rejected(self):
        for field, value in (('observations', 35), ('variants', 2), ('native_cumulative_bit_identical', False)):
            data = row()
            data['contrast'][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                summary.inspect_trace(data, 1)

    def test_baseline_hash_rejected_before_report_access(self):
        with tempfile.TemporaryDirectory() as directory:
            baseline = Path(directory) / 'baseline.json'
            baseline.write_text('{}')
            with self.assertRaisesRegex(ValueError, 'baseline hash mismatch'):
                summary.load_checked(Path(directory) / 'missing.json', baseline, False)

    def test_stage_summary_distinguishes_raw_and_compressed(self):
        rows = []
        for tonic in range(12):
            item = row()
            item.update(kind='pcm_cumulative', condition=0, stimulus_mode=0, window=1,
                        stimulus_tonic=tonic, chroma=copy.deepcopy(item['contrast']['compressed_window']))
            rows.append(item)
        group = summary.summarize_rows(rows)[0]
        self.assertLess(group['stages']['raw_folded']['mean_min_over_mean'],
                        group['stages']['compressed_window']['mean_min_over_mean'])
        self.assertEqual(group['stages']['compressed_window'], group['stages']['native_cumulative'])
        with self.assertRaisesRegex(ValueError, 'incomplete transposition'):
            summary.summarize_rows(rows[:-1])


if __name__ == '__main__':
    unittest.main()
