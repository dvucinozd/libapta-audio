#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import apta_1_1_native_annotated_development as module


class DenseReferenceTests(unittest.TestCase):
    def fixture(self):
        truth = {"beat_frames": list(range(0, 312000, 24000)), "downbeat_frames": [0, 96000, 192000, 288000],
                 "meter_numerator": 4, "meter_denominator": 4}
        window = {"first_frame": 0, "end_frame": 262144,
                  "selected": {"kind": "accepted", "anchor_frame": 0, "tempo_millibpm": 120000, "confidence": 80},
                  "meter": {"kind": "estimated", "numerator": 4, "denominator": 4, "downbeat_frame": 0, "downbeat_fraction_q32": 0}}
        return truth, window

    def test_dense_source_coordinates_and_meter(self):
        truth, window = self.fixture()
        result = module.score_window(window, truth, 48000)
        self.assertEqual(result["confirmed_joint_windows"], 1)
        self.assertEqual(result["meter_correct"], 1)
        self.assertEqual(result["downbeat_correct"], 1)
        self.assertEqual(result["predicted_beats"], result["scored_beats"])
        shift = 262144
        shifted_truth = dict(truth, beat_frames=[value + shift for value in truth["beat_frames"]],
                             downbeat_frames=[value + shift for value in truth["downbeat_frames"]])
        shifted = copy.deepcopy(window)
        shifted.update(first_frame=shift, end_frame=window["end_frame"] + shift)
        shifted["selected"]["anchor_frame"] += shift
        shifted["meter"]["downbeat_frame"] += shift
        self.assertEqual(module.score_window(shifted, shifted_truth, 48000), result)

    def test_half_tempo_and_expressive_reference_are_not_joint_success(self):
        truth, window = self.fixture()
        window["selected"]["tempo_millibpm"] = 60000
        result = module.score_window(window, truth, 48000)
        self.assertEqual(result["confirmed_phase_windows"], 1)
        self.assertEqual(result["confirmed_period_windows"], 0)
        self.assertEqual(result["confirmed_joint_windows"], 0)
        truth, window = self.fixture()
        truth["beat_frames"] = [0, 24000, 48000, 72000, 100000, 128000, 156000, 184000, 212000, 240000, 268000, 296000]
        self.assertEqual(module.score_window(window, truth, 48000)["confirmed_joint_windows"], 0)

    def test_boundary_context_and_abstention_are_explicit(self):
        truth, window = self.fixture()
        truth["beat_frames"] = truth["beat_frames"][1:]
        result = module.score_window(window, truth, 48000)
        self.assertEqual(result["reference_complete_windows"], 0)
        self.assertEqual(result["confirmed_joint_windows"], 0)
        self.assertEqual(result["high_confidence_grid_unknown_context"], 1)
        self.assertEqual(result["high_confidence_grid_error"], 0)
        truth, window = self.fixture()
        window["selected"] = {"kind": "rejected"}
        result = module.score_window(window, truth, 48000)
        self.assertEqual(result["reference_complete_windows"], 1)
        self.assertEqual(result["confirmed_joint_windows"], 0)
        self.assertEqual(result["meter_emitted"], 0)

    def test_holdout_or_undeclared_preparation_rejected_before_label_read(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for splits in [None, ["development", "holdout"], ["holdout"]]:
                (root / "manifest.json").write_text(json.dumps({"format": module.canonical.FORMAT,
                    "prepared_splits": splits, "holdout_performances_opened": False}))
                with mock.patch.object(module.canonical, "load_prepared") as labels:
                    with self.assertRaises(ValueError):
                        module.load_development(root)
                    labels.assert_not_called()

    def test_ballroom_declaration_and_balanced_cohort(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            declaration = {"format": module.ballroom.FORMAT, "prepared_splits": ["development"],
                "holdout_performances_opened": False, "audio_archive_md5": module.ballroom.AUDIO_ARCHIVE_MD5,
                "annotation_archive_md5": module.ballroom.ANNOTATION_ARCHIVE_MD5,
                "selection_seed": module.ballroom.SELECTION_SEED, "compressed_distribution_contains_holdout": True}
            rows = [dict(self.fixture()[0], track=f"track-{index:024x}", split="development",
                         meter_numerator=3 if index < 20 else 4) for index in range(40)]
            labels_path = root / "labels.json"
            labels_path.write_text(json.dumps(rows))
            declaration.update(labels_sha256=module.canonical.sha256_file(labels_path), track_ids=[row["track"] for row in rows])
            (root / "manifest.json").write_text(json.dumps(declaration))
            self.assertEqual(module.load_development(root)[1], rows)
            for field, bad in (("prepared_splits", ["holdout"]), ("audio_archive_md5", "0" * 32),
                               ("annotation_archive_md5", "0" * 32), ("selection_seed", "changed"),
                               ("holdout_performances_opened", True), ("compressed_distribution_contains_holdout", False)):
                (root / "manifest.json").write_text(json.dumps(dict(declaration, **{field: bad})))
                with mock.patch.object(module.canonical, "load_prepared") as read:
                    with self.assertRaises(ValueError):
                        module.load_development(root)
                    read.assert_not_called()
            (root / "manifest.json").write_text(json.dumps(declaration))
            for bad_rows in (rows[:-1], [dict(row, meter_numerator=4) for row in rows], [dict(row, split="holdout") for row in rows]):
                with mock.patch.object(module.canonical, "load_prepared", return_value=(declaration, bad_rows)):
                    with self.assertRaises(ValueError):
                        module.load_development(root)

    def test_asap_duration_remains_strict_and_ballroom_preserves_duration(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for corpus_format, should_run in ((module.canonical.FORMAT, False), (module.ballroom.FORMAT, True)):
                prepared = root / ("asap" if not should_run else "ballroom")
                prepared.mkdir()
                (prepared / "manifest.json").write_text("fixture")
                manifest = {"format": corpus_format, "track_ids": ["opaque"]}
                frozen = {"tracks": [{"track": "opaque", "source_total_frames": 1400000}]}
                with mock.patch.object(module, "load_development", return_value=(manifest, [])), \
                     mock.patch.object(module.history, "freeze", return_value=frozen), \
                     mock.patch.object(module.history, "run", side_effect=ValueError("reached native runner")) as run:
                    with self.assertRaisesRegex(ValueError, "reached native runner" if should_run else "thirty-second"):
                        module.run(prepared, root / "binary", prepared / "output", "revision", "inventory")
                    self.assertEqual(run.call_count, int(should_run))


if __name__ == "__main__":
    unittest.main()
