#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
import wave

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import apta_1_1_pcm_anchor_timing as module


class PcmAnchorTests(unittest.TestCase):
    def fixture(self, total=20000, anchor=6000, offset=240, amplitude=10000):
        pcm = np.zeros((total, 2), dtype=np.int16)
        pcm[anchor + offset:] = amplitude
        return pcm, [anchor], [{"first_frame": 0, "end_frame": total}]

    def apply(self, pcm, beats, windows, downbeats=None, gaps=()):
        return module.refine(module.energy_prefix(pcm), beats, downbeats or [], windows, gaps)

    def test_known_step_has_exact_unbiased_source_coordinate(self):
        pcm, beats, windows = self.fixture()
        result = self.apply(pcm, beats, windows)
        self.assertEqual(result["beat_frames"], [6240])
        self.assertEqual(result["records"][0]["state"], "supported")
        self.assertEqual(result["records"][0]["ordinal"], 0)

    def test_opposite_stereo_polarity_cannot_cancel_attack_evidence(self):
        pcm, beats, windows = self.fixture()
        same = self.apply(pcm, beats, windows)
        pcm[:, 1] *= -1
        self.assertEqual(self.apply(pcm, beats, windows), same)

    def test_integer_energy_extremes_do_not_overflow(self):
        pcm = np.full((1440000, 2), -32768, dtype=np.int16)
        prefix = module.energy_prefix(pcm)
        self.assertEqual(int(prefix[-1]), 1440000 * 2 * 32768 ** 2)
        self.assertTrue(np.all(prefix[1:] > prefix[:-1]))

    def test_silence_and_constant_magnitude_are_not_attack_support(self):
        pcm, beats, windows = self.fixture(amplitude=0)
        for values in (pcm, np.full_like(pcm, 10000), np.tile(np.array([[10000, -10000], [-10000, 10000]], dtype=np.int16), (len(pcm) // 2, 1))):
            result = self.apply(values, beats, windows)
            self.assertEqual(result["beat_frames"], beats)
            self.assertEqual(result["records"][0]["state"], "unrefined_no_rise")

    def test_equal_separated_attacks_are_ambiguous_without_weaker_peak_rescue(self):
        pcm, beats, windows = self.fixture(amplitude=0)
        for onset in (5520, 6480):
            pcm[onset:onset + 192] = 10000
        result = self.apply(pcm, beats, windows)
        self.assertEqual(result["beat_frames"], beats)
        self.assertEqual(result["records"][0]["state"], "unrefined_ambiguous")

    def test_weak_rise_retains_original_anchor(self):
        pcm, beats, windows = self.fixture(amplitude=32)
        result = self.apply(pcm, beats, windows)
        self.assertEqual(result["beat_frames"], beats)
        self.assertEqual(result["records"][0]["state"], "unrefined_low_energy")

    def test_small_rise_on_loud_sustain_fails_normalized_contrast(self):
        pcm, beats, windows = self.fixture(amplitude=11000)
        pcm[:6240] = 10000
        result = self.apply(pcm, beats, windows)
        self.assertEqual(result["beat_frames"], beats)
        self.assertEqual(result["records"][0]["state"], "unrefined_low_contrast")

    def test_search_edge_is_never_reported_as_refined(self):
        for offset in (-960, 960):
            pcm, beats, windows = self.fixture(offset=offset)
            result = self.apply(pcm, beats, windows)
            self.assertEqual(result["records"][0]["state"], "unrefined_search_edge")
            self.assertEqual(result["beat_frames"], beats)

    def test_incomplete_source_context_is_not_zero_padded(self):
        for anchor in (100, 19900):
            pcm, beats, windows = self.fixture(anchor=anchor, offset=0)
            result = self.apply(pcm, beats, windows)
            self.assertEqual(result["beat_frames"], beats)
            self.assertEqual(result["records"][0]["state"], "unrefined_source_boundary")

    def test_attack_cannot_move_event_across_original_s6_boundary(self):
        pcm, beats, _ = self.fixture()
        windows = [{"first_frame": 0, "end_frame": 6100}, {"first_frame": 6100, "end_frame": len(pcm)}]
        result = self.apply(pcm, beats, windows)
        self.assertEqual(result["records"][0]["state"], "unrefined_ownership")
        self.assertEqual(result["beat_frames"], beats)

    def test_midpoint_cells_preserve_neighbor_identity_and_order(self):
        pcm, _, windows = self.fixture(offset=480)
        result = self.apply(pcm, [6000, 6500], windows)
        self.assertEqual(result["records"][0]["state"], "unrefined_ownership")
        self.assertEqual(result["beat_frames"][0], 6000)
        self.assertGreater(result["beat_frames"][1], 6250)

    def test_known_gap_is_unavailable_even_when_pcm_contains_a_plausible_attack(self):
        pcm, beats, windows = self.fixture()
        result = self.apply(pcm, beats, windows, gaps=((6200, 6300),))
        self.assertEqual(result["records"][0]["state"], "unavailable_pcm_gap")
        self.assertEqual(result["beat_frames"], beats)
        self.assertEqual(module.project_windows(result, windows)[0]["selected"]["kind"], "insufficient_pcm")

    def test_gap_elsewhere_still_abstains_affected_window_projection(self):
        pcm, beats, windows = self.fixture()
        result = self.apply(pcm, beats, windows, gaps=((15000, 16000),))
        self.assertEqual(result["records"][0]["state"], "supported")
        self.assertEqual(module.project_windows(result, windows)[0]["selected"]["kind"], "insufficient_pcm")

    def test_source_shift_preserves_relative_time_without_global_bin_rounding(self):
        pcm, beats, windows = self.fixture()
        original = self.apply(pcm, beats, windows)
        shift = 12345
        shifted_pcm = np.vstack((np.zeros((shift, 2), dtype=np.int16), pcm))
        shifted = self.apply(shifted_pcm, [p + shift for p in beats], [{"first_frame": 0, "end_frame": len(shifted_pcm)}])
        self.assertEqual(shifted["beat_frames"], [p + shift for p in original["beat_frames"]])

    def test_all_events_and_downbeat_indices_survive_on_the_same_grid(self):
        total = 400000
        beats = list(range(10000, 380000, 24000))
        pcm = np.zeros((total, 2), dtype=np.int16)
        for frame in beats:
            pcm[frame + 240:frame + 432] = 10000
        windows = [{"first_frame": 0, "end_frame": total}]
        result = self.apply(pcm, beats, windows, beats[::4])
        self.assertEqual(result["beat_frames"], [p + 240 for p in beats])
        self.assertEqual(result["downbeat_frames"], result["beat_frames"][::4])
        self.assertEqual([r["ordinal"] for r in result["records"]], list(range(len(beats))))
        projected = module.project_windows(result, windows)[0]
        expected = module.ref.project(result["beat_frames"], result["downbeat_frames"], 0, total)
        self.assertEqual(projected, expected)
        self.assertEqual(projected["meter"]["kind"], "estimated")
        points = module.representation.points_for_control(projected)
        encoded = projected["meter"]["downbeat_frame"] * (1 << 32) + projected["meter"]["downbeat_fraction_q32"]
        self.assertIn(encoded, [p for p, _ in points])

    def test_missing_extra_and_half_double_beats_are_never_filled_or_reselected(self):
        total = 400000
        original = list(range(10000, 380000, 24000))
        pcm = np.zeros((total, 2), dtype=np.int16)
        windows = [{"first_frame": 0, "end_frame": total}]
        cases = [original[::2], list(range(10000, 380000, 12000)), original[:4] + original[5:], sorted(original + [40000])]
        for beats in cases:
            result = self.apply(pcm, beats, windows)
            self.assertEqual(result["beat_frames"], beats)
            self.assertEqual(module.project_windows(result, windows)[0], module.ref.project(beats, [], 0, total))

    def test_wave_reader_preserves_stereo_source_geometry(self):
        pcm, _, _ = self.fixture()
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "synthetic.wav"
            with wave.open(str(path), "wb") as output:
                output.setparams((2, 2, 48000, 0, "NONE", "not compressed"))
                output.writeframes(pcm.tobytes())
            np.testing.assert_array_equal(module.read_prefix(path, len(pcm) - 100, len(pcm)), module.energy_prefix(pcm[:-100]))
            with self.assertRaises(ValueError):
                module.read_prefix(path, len(pcm), len(pcm) + 1)
            path.write_bytes(path.read_bytes()[:-100])
            with self.assertRaises(ValueError):
                module.read_prefix(path, len(pcm), len(pcm))

    def test_invalid_samples_prefix_events_gaps_and_partition_fail_before_search(self):
        for pcm in (np.zeros((10, 2)), np.zeros((10, 1), dtype=np.int16), np.zeros((0, 2), dtype=np.int16), np.zeros((1440001, 2), dtype=np.int16)):
            with self.assertRaises(ValueError):
                module.energy_prefix(pcm)
        pcm, beats, windows = self.fixture()
        for prefix in (np.array([], dtype=np.int64), np.array([1, 2], dtype=np.int64), np.array([0, 10, 9], dtype=np.int64), np.array([0., 1.])):
            with self.assertRaises(ValueError):
                module.refine(prefix, [], [], windows)
        for events in ([6000, 6000], [-1], [20000], list(range(3073))):
            with self.assertRaises(ValueError):
                self.apply(pcm, events, windows)
        for gaps in (((10, 5),), ((10, 30), (20, 40)), ((0, 20001),)):
            with self.assertRaises(ValueError):
                self.apply(pcm, beats, windows, gaps=gaps)
        with self.assertRaises(ValueError):
            self.apply(pcm, beats, [{"first_frame": 0, "end_frame": 19999}])
        with self.assertRaises(ValueError):
            self.apply(pcm, beats, windows, [6100])

    def test_input_arrays_and_events_remain_unchanged(self):
        pcm, beats, windows = self.fixture()
        prefix = module.energy_prefix(pcm)
        saved = prefix.copy()
        module.refine(prefix, beats, [], windows)
        np.testing.assert_array_equal(prefix, saved)
        self.assertEqual(beats, [6000])

    def test_aggregate_gain_does_not_override_any_old_correct_loss(self):
        totals = {"candidate": {"confirmed_joint_windows": 200}, "common_candidate": {"complete_sequence_windows": 190}}
        changes = {"ols": {k + "_breaks": 0 for k in ("period", "phase", "joint", "meter", "downbeat")}}
        self.assertTrue(all(module.gates(totals, changes, 0).values()))
        self.assertFalse(all(module.gates(totals, changes, 1).values()))
        changes["ols"]["meter_breaks"] = 1
        self.assertFalse(all(module.gates(totals, changes, 0).values()))

    def test_protocol_byte_hash_is_enforced(self):
        self.assertFalse(module.protocol()["formal_acceptance"])
        with mock.patch.object(module.ref.history, "sha256_file", return_value="0" * 64):
            with self.assertRaises(ValueError):
                module.protocol()


if __name__ == "__main__":
    unittest.main()
