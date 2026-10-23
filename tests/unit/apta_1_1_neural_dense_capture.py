#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import math
import struct
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import apta_neural_dense as codec
import apta_1_1_neural_dense_capture as capture


class DenseCaptureTests(unittest.TestCase):
    def windows(self, total=1440000):
        bounds = list(range(0, total, 262144)) + [total]
        return [{"first_frame": a, "end_frame": b} for a, b in zip(bounds, bounds[1:])]

    def test_exact_interleaved_little_endian_payload(self):
        payload = codec.encode([1., -2., .25], [-3., 4., -.5])
        self.assertEqual(payload, struct.pack("<8sII6f", b"APTADN01", 3, 2, 1., -3., -2., 4., .25, -.5))
        self.assertEqual(codec.decode(payload), ([1., -2., .25], [-3., 4., -.5]))

    def test_f32_signed_zero_subnormal_and_extreme_finite_bits_survive(self):
        payload = struct.pack("<8sII", b"APTADN01", 3, 2) + struct.pack("<6I", 0, 0x80000000, 1, 0x80000001, 0x7f7fffff, 0xff7fffff)
        decoded = codec.decode(payload)
        self.assertEqual(codec.encode(*decoded), payload)
        self.assertEqual(math.copysign(1, decoded[1][0]), -1)

    def test_no_threshold_or_sigmoid_is_applied(self):
        beat, downbeat = [-1000., -.001, 0., .001, 1000.], [0.] * 5
        raw = codec.encode(beat, downbeat)
        decoded = codec.decode(raw)
        self.assertEqual(raw, codec.encode(*decoded))
        stats = codec.statistics(*decoded)
        self.assertEqual(stats["beat"]["values"], 5)
        self.assertEqual(stats["beat"]["positive_logits"], 2)
        self.assertEqual(stats["beat"]["nonpositive_logits"], 3)

    def test_missing_extra_and_reordered_framing_rejected(self):
        payload = codec.encode([1., 2.], [3., 4.])
        for invalid in (payload[:-1], payload + b"\0", b"UNKNOWN!" + payload[8:], payload[:12] + struct.pack("<I", 1) + payload[16:]):
            with self.assertRaises(ValueError):
                codec.decode(invalid)
        # A row permutation is detected by external bound payload hashes, not by
        # guessing signal values. The framing itself cannot certify chronology.
        changed = payload[:16] + payload[24:] + payload[16:24]
        self.assertNotEqual(changed, payload)

    def test_invalid_shape_counts_and_f32_overflow_fail(self):
        for beat, db in (([], []), ([1.], []), ([0.] * 1502, [0.] * 1502), ([1e300], [0.])):
            with self.assertRaises(ValueError):
                codec.encode(beat, db)
        for rows in (0, 1502, 0xffffffff):
            with self.assertRaises(ValueError):
                codec.decode(struct.pack("<8sII", b"APTADN01", rows, 2))

    def test_nonfinite_encoding_decoding_and_statistics_fail(self):
        for value in (math.nan, math.inf, -math.inf):
            with self.assertRaises(ValueError):
                codec.encode([value], [0.])
            with self.assertRaises(ValueError):
                codec.decode(struct.pack("<8sIIff", b"APTADN01", 1, 2, value, 0.))
            with self.assertRaises(ValueError):
                codec.statistics([0.], [value])

    def test_fullsize_payload_is_bounded(self):
        payload = codec.encode([0.] * 1501, [-1.] * 1501)
        self.assertEqual(len(payload), 12024)
        self.assertEqual(len(codec.decode(payload)[0]), 1501)

    def test_short_upstream_chunk_assigns_all_rows(self):
        result = codec.coverage(101, [-6], [113])
        self.assertEqual(result["assignment_ranges"], [[0, 101]])
        self.assertEqual(result["minimum_assignments"], 1)

    def test_1501_row_schedule_retains_overlap_with_keep_first(self):
        result = codec.coverage(1501, [-6, 7], [1500, 1500])
        self.assertEqual(result["assignment_ranges"], [[0, 1488], [13, 1501]])
        self.assertEqual(result["assigned_rows"], 1501)
        self.assertEqual(result["maximum_assignments"], 2)

    def test_unassigned_or_outside_or_wrong_border_schedule_fails(self):
        for rows, starts, lengths in ((1501, [-6], [1500]), (101, [-5], [113]), (101, [-6], [12]),
                                      (101, [-6, -7], [113, 113]), (101, [], [])):
            with self.assertRaises(ValueError):
                codec.coverage(rows, starts, lengths)
        with self.assertRaises(ValueError):
            codec.coverage(101, [-6], [113], border=5)

    def test_source_endpoint_and_original_s6_centers_partition_all_rows(self):
        result = codec.mapping(1440000, 661500, 1501, self.windows())
        self.assertEqual(result["exclusive_endpoint_rows"], [1500])
        self.assertEqual(result["beyond_source_rows"], [])
        self.assertEqual(sum(w["end_row"] - w["first_row"] for w in result["windows"]), 1500)
        self.assertEqual(result["center_hop_source_frames"], 960)
        self.assertFalse(result["causal_availability"])

    def test_noninteger_resample_lengths_keep_floor_or_ceil_geometry(self):
        for total in (1436735, 1439661):
            lower, upper = total * 22050 // 48000, (total * 22050 + 47999) // 48000
            for mono in (lower, upper):
                result = codec.mapping(total, mono, mono // 441 + 1, self.windows(total))
                all_rows = [i for w in result["windows"] for i in range(w["first_row"], w["end_row"])]
                self.assertEqual(sorted(all_rows + result["exclusive_endpoint_rows"] + result["beyond_source_rows"]), list(range(result["rows"])))

    def test_centered_beyond_source_row_is_retained_not_silently_clipped(self):
        # A fractional source duration can round its mono end to the next hop.
        result = codec.mapping(959, 441, 2, [{"first_frame": 0, "end_frame": 959}])
        self.assertEqual(result["beyond_source_rows"], [1])
        self.assertEqual(result["windows"][0]["end_row"], 1)

    def test_source_window_tail_shape_or_resample_mismatch_fails(self):
        for total, mono, rows, windows in ((1440000, 661500, 1500, self.windows()),
                (1440000, 661400, 1500, self.windows()), (1440001, 661501, 1501, self.windows()),
                (1440000, 661500, 1501, [{"first_frame": 0, "end_frame": 1400000}])):
            with self.assertRaises(ValueError):
                codec.mapping(total, mono, rows, windows)

    def test_window_metadata_cannot_carry_labels(self):
        windows = self.windows()
        windows[0]["labels"] = []
        with self.assertRaises(ValueError):
            codec.mapping(1440000, 661500, 1501, windows)

    def test_event_adapter_keeps_exclusive_endpoint_separate(self):
        events = capture.event_record([0., .5, 1.], [0., 1.], 48000)
        self.assertEqual(events["beat_frames"], [0, 24000])
        self.assertEqual(events["downbeat_frames"], [0])
        self.assertEqual(events["exclusive_endpoint_events"], {"beat_seconds": [1.], "downbeat_seconds": [1.]})
        self.assertIsNone(events["confidence"])

    def test_event_adapter_rejects_later_negative_unsorted_or_offgrid_downbeats(self):
        for beats, db in (([0., 1.01], []), ([-.01, .5], []), ([.5, .1], []), ([0., .5], [.25])):
            with self.assertRaises(ValueError):
                capture.event_record(beats, db, 48000)

    def test_protocol_is_bound_without_parsing_labels(self):
        self.assertFalse(capture.protocol()["formal_acceptance"])
        with mock.patch.object(capture.ref.history, "sha256_file", return_value="0" * 64):
            with self.assertRaises(ValueError):
                capture.protocol()


if __name__ == "__main__":
    unittest.main()
