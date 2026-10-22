#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import copy
import math
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import apta_1_1_neural_reference as module


class NeuralProjectionTests(unittest.TestCase):
    def fixture(self):
        beats = list(range(0, 336001, 24000))
        db = [0, 96000, 192000, 288000]
        truth = {"beat_frames": beats, "downbeat_frames": db,
                 "meter_numerator": 4, "meter_denominator": 4}
        return beats, db, truth

    def test_constant_grid_and_same_grid_meter(self):
        beats, db, truth = self.fixture()
        value = module.project(beats, db, 0, 262144)
        self.assertEqual(value["selected"]["tempo_millibpm"], 120000)
        result = module.annotated.score_window(value, truth, module.RATE)
        self.assertEqual(result["confirmed_joint_windows"], 1)
        self.assertEqual(result["meter_correct"], 1)
        self.assertEqual(result["downbeat_correct"], 1)
        self.assertEqual(value["selected"]["confidence"], 0)

    def test_source_offset_preserved(self):
        beats, db, truth = self.fixture()
        old = module.project(beats, db, 0, 262144)
        shift = 262144
        new = module.project([b + shift for b in beats], [b + shift for b in db], shift, shift + 262144)
        self.assertEqual(new["selected"]["anchor_frame"], old["selected"]["anchor_frame"] + shift)
        self.assertEqual(new["meter"]["downbeat_frame"], old["meter"]["downbeat_frame"] + shift)

    def test_half_and_double_tempo_fail_period_despite_aligned_phase(self):
        beats, db, truth = self.fixture()
        for events in (beats[::2], list(range(0, 336001, 12000))):
            value = module.project(events, [], 0, 262144)
            scored = module.annotated.score_window(value, truth, module.RATE)
            self.assertEqual(scored["confirmed_joint_windows"], 0)
            self.assertEqual(scored["confirmed_period_windows"], 0)

    def test_missing_events_and_variable_timing_are_not_oracle_success(self):
        beats, db, truth = self.fixture()
        incomplete = [b for i, b in enumerate(beats) if i not in (4, 7)]
        value = module.project(incomplete, [], 0, 262144)
        self.assertEqual(module.annotated.score_window(value, truth, module.RATE)["confirmed_joint_windows"], 0)
        expressive = [0, 24000, 48000, 72000, 100000, 128000, 156000, 184000, 212000, 240000, 268000, 296000]
        truth = dict(truth, beat_frames=expressive, downbeat_frames=[0, 100000, 212000])
        value = module.project(expressive, truth["downbeat_frames"], 0, 262144)
        self.assertEqual(module.annotated.score_window(value, truth, module.RATE)["confirmed_joint_windows"], 0)

    def test_context_incomplete_is_unknown_and_insufficient_support_abstains(self):
        beats, db, truth = self.fixture()
        truth = dict(truth, beat_frames=beats[1:])
        value = module.project(beats, db, 0, 262144)
        score = module.annotated.score_window(value, truth, module.RATE)
        self.assertEqual(score["reference_complete_windows"], 0)
        self.assertEqual(score["confirmed_joint_windows"], 0)
        self.assertEqual(module.project(beats[:3], [], 0, 262144)["selected"]["kind"], "insufficient_events")

    def test_invalid_events_downbeats_and_geometry_refused(self):
        for bad in ([0, 10, 10], [0, 20, 10], [-1, 2], [0, math.nan], [0, math.inf], [False, 20]):
            with self.assertRaises(ValueError):
                module.project(bad, [], 0, 100)
        with self.assertRaises(ValueError):
            module.project([0, 10, 20, 30], [15], 0, 100)
        with self.assertRaises(ValueError):
            module.project([], [], 100, 100)

    def test_meter_requires_two_equal_complete_bars(self):
        beats, db, _ = self.fixture()
        for downbeats in ([], db[:2], [0, 72000, 192000]):
            self.assertFalse(module.project(beats, downbeats, 0, 262144)["meter"])
        triple = module.project(beats, [0, 72000, 144000, 216000], 0, 262144)
        self.assertEqual(triple["meter"]["numerator"], 3)

    def test_protocol_hash_and_private_output_boundary(self):
        self.assertFalse(module.protocol()["formal_acceptance"])
        with self.assertRaises(ValueError):
            module.external(module.ROOT / "private-data")
        with mock.patch.object(module.history, "sha256_file", return_value="0" * 64):
            with self.assertRaises(ValueError):
                module.protocol()

    def test_artifact_mutation_refused(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "artifact"
            path.write_text("original")
            bindings = {}
            module.bind(path, module.history.sha256_file(path), bindings)
            path.write_text("changed")
            with self.assertRaises(ValueError):
                module.recheck(bindings)

    def test_selection_is_metadata_only_balanced_deterministic_and_deduplicated(self):
        index = {"tracks": {f"g{g}.{i:05d}": {"audio": ["unused", f"{g}-{i}"]}
                              for g in range(10) for i in range(15)}}
        original = copy.deepcopy(index)
        selected = module.select_ids(index)
        self.assertEqual(len(selected), 100)
        self.assertEqual(index, original)
        self.assertEqual(selected, module.select_ids(index))
        available = set(index["tracks"]) - {selected[0]}
        repaired = module.select_ids(index, available)
        self.assertEqual(len(repaired), 100)
        self.assertNotIn(selected[0], repaired)
        self.assertTrue(all(sum(x.startswith(f"g{g}.") for x in selected) == 10 for g in range(10)))
        index["tracks"]["g0.99999"] = {"audio": ["unused", "0-0"]}
        self.assertEqual(selected, module.select_ids(index))


if __name__ == "__main__":
    unittest.main()
