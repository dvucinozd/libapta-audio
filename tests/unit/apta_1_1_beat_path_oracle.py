#!/usr/bin/env python3
"""Unit tests for the frozen WP2-I3 beat-path development oracle."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from unittest import mock

import numpy as np


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import apta_1_1_beat_path_oracle as oracle  # noqa: E402


class BeatPathOracleTests(unittest.TestCase):
    def test_offset_order_prefers_zero_then_small_distance(self) -> None:
        self.assertEqual(oracle._offset_states(3), [0, -1, 1, -2, 2, -3, 3])

    def test_stable_pulse_has_more_support_than_erratic_offsets(self) -> None:
        lag = 100
        phase = 50
        stable = np.full(oracle.TRACE_BINS, 0.01, dtype=np.float64)
        erratic = stable.copy()
        for index, beat in enumerate(range(phase, oracle.TRACE_BINS, lag)):
            stable[beat] = 1.0
            offset = 9 if index % 2 == 0 else -9
            if 0 <= beat + offset < oracle.TRACE_BINS:
                erratic[beat + offset] = 1.0
        stable_score = oracle._path_mean(stable, lag, phase)
        erratic_score = oracle._path_mean(erratic, lag, phase)
        self.assertIsNotNone(stable_score)
        self.assertIsNotNone(erratic_score)
        self.assertGreater(stable_score, erratic_score)

    def test_silence_preserves_rank_zero(self) -> None:
        novelty = np.zeros(oracle.TRACE_BINS, dtype=np.float64)
        candidates = [(100, 1.0), (120, 0.9), (80, 0.8)]
        with mock.patch.object(oracle, "_candidate_lags", return_value=candidates):
            rank, rows = oracle._select_beat_path_rank(novelty, 48000)
        self.assertEqual(rank, 0)
        self.assertEqual([row["lag"] for row in rows], [100, 120, 80])

    def test_path_support_can_promote_existing_runner_up(self) -> None:
        novelty = np.full(oracle.TRACE_BINS, 0.01, dtype=np.float64)
        for beat in range(20, oracle.TRACE_BINS, 120):
            novelty[beat] = 1.0
        candidates = [(100, 1.0), (120, 0.99), (80, 0.5)]

        def phase(_novelty: np.ndarray, lag: int) -> int:
            return 20 if lag == 120 else 0

        with (
            mock.patch.object(oracle, "_candidate_lags", return_value=candidates),
            mock.patch.object(oracle, "_selected_phase", side_effect=phase),
        ):
            rank, rows = oracle._select_beat_path_rank(novelty, 48000)
        self.assertEqual(rank, 1)
        self.assertEqual(int(rows[rank]["lag"]), 120)

    def test_equal_totals_preserve_original_rank(self) -> None:
        novelty = np.ones(oracle.TRACE_BINS, dtype=np.float64)
        candidates = [(100, 1.0), (100, 1.0), (100, 1.0)]
        with mock.patch.object(oracle, "_candidate_lags", return_value=candidates):
            rank, _rows = oracle._select_beat_path_rank(novelty, 48000)
        self.assertEqual(rank, 0)

    def test_rejects_wrong_trace_geometry_and_negative_values(self) -> None:
        with self.assertRaisesRegex(ValueError, "4,096-bin"):
            oracle._path_mean(np.zeros(10), 4, 0)
        negative = np.zeros(oracle.TRACE_BINS, dtype=np.float64)
        negative[0] = -1.0
        with self.assertRaisesRegex(ValueError, "non-negative"):
            oracle._path_mean(negative, 100, 0)


if __name__ == "__main__":
    unittest.main()
