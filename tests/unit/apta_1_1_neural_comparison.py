#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Optional host-model suite; requires the isolated mir_eval environment."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import apta_1_1_neural_comparison as module


class EventScoringTests(unittest.TestCase):
    def fixture(self):
        return {"beat_frames": list(range(0, 1440000, 24000)),
                "downbeat_frames": list(range(0, 1440000, 96000))}

    def test_exact_source_events_and_five_second_trim(self):
        truth = self.fixture()
        self.assertTrue(all(v == 1 for v in module.event_metrics(truth, truth).values()))
        pred = {k: [x for x in v if x >= 240000] for k, v in truth.items()}
        self.assertEqual(module.event_metrics(truth, pred), module.event_metrics(truth, truth))

    def test_wrong_phase_and_empty_events_cannot_be_dropped(self):
        truth = self.fixture()
        pred = {k: [x + 9600 for x in v if x + 9600 < 1440000] for k, v in truth.items()}
        result = module.event_metrics(truth, pred)
        self.assertEqual(result["beat_f1"], 0)
        self.assertEqual(result["downbeat_f1"], 0)
        self.assertEqual(result["beat_cmlt"], 0)
        # CMLt's relative tolerance can accept a 200 ms downbeat shift at
        # 120 BPM / 4 beats per bar; it must never replace event F1/APTA phase.
        self.assertGreater(result["downbeat_cmlt"], 0)
        self.assertTrue(all(v == 0 for v in module.event_metrics(truth, {k: [] for k in truth}).values()))

    def test_continuity_penalizes_half_tempo_even_with_aligned_events(self):
        truth = self.fixture()
        pred = {k: v[::2] for k, v in truth.items()}
        result = module.event_metrics(truth, pred)
        self.assertGreater(result["beat_f1"], .5)
        self.assertEqual(result["beat_cmlt"], 0)


if __name__ == "__main__":
    unittest.main()
