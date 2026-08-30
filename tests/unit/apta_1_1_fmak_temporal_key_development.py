#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))
SPEC = importlib.util.spec_from_file_location(
    "apta_1_1_fmak_temporal_key_development",
    TOOLS / "apta_1_1_fmak_temporal_key_development.py",
)
assert SPEC is not None and SPEC.loader is not None
module = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = module
SPEC.loader.exec_module(module)


def synthetic_inventory() -> list[module.Candidate]:
    rows: list[module.Candidate] = []
    source_id = 1
    classes = [(tonic, mode) for mode in ("major", "minor") for tonic in range(12)]
    for index, (tonic, mode) in enumerate(classes):
        count = 28 if index == len(classes) - 1 else 29
        for _unused in range(count):
            rows.append(module.Candidate(source_id, tonic, mode))
            source_id += 1
    assert len(rows) == module.ELIGIBLE_COUNT
    return rows


class FmakTemporalKeyDevelopmentTests(unittest.TestCase):
    def test_normalizes_enharmonics_and_rejects_unknown_labels(self) -> None:
        self.assertEqual(module.normalize_key("Db Major"), (1, "major"))
        self.assertEqual(module.normalize_key("B♭ minor"), (10, "minor"))
        with self.assertRaises(module.shared.ValidationError):
            module.normalize_key("H major")

    def test_selection_is_deterministic_and_exactly_class_balanced(self) -> None:
        rows = synthetic_inventory()
        first = module.select_candidates(rows)
        second = module.select_candidates(reversed(rows))
        self.assertEqual(first, second)
        self.assertEqual(len(first), 96)
        self.assertEqual(module.selection_sha256(first), module.selection_sha256(second))
        for tonic in range(12):
            for mode in ("major", "minor"):
                self.assertEqual(
                    sum(
                        row.key_tonic == tonic and row.key_mode == mode
                        for row in first
                    ),
                    4,
                )

    def test_selection_rejects_inventory_drift_and_short_class(self) -> None:
        rows = synthetic_inventory()
        with self.assertRaises(module.shared.ValidationError):
            module.select_candidates(rows[:-1])
        shortened = [
            row
            for row in rows
            if not (row.key_tonic == 0 and row.key_mode == "major" and row.source_id > 3)
        ]
        while len(shortened) < module.ELIGIBLE_COUNT:
            source_id = 10_000 + len(shortened)
            shortened.append(module.Candidate(source_id, 1, "major"))
        with self.assertRaises(module.shared.ValidationError):
            module.select_candidates(shortened)

    def test_archive_member_lookup_is_exact_and_path_independent(self) -> None:
        selected = [
            module.Candidate(10, 0, "major"),
            module.Candidate(141, 1, "minor"),
        ]
        with tempfile.TemporaryDirectory() as directory:
            archive_path = Path(directory) / "fixture.zip"
            with zipfile.ZipFile(archive_path, "w") as archive:
                archive.writestr("000/000010.mp3", b"ten")
                archive.writestr("nested/000141.mp3", b"one-forty-one")
            with zipfile.ZipFile(archive_path) as archive:
                members = module._archive_members(archive, selected)
            self.assertEqual(set(members), {10, 141})
            self.assertEqual(members[10].filename, "000/000010.mp3")

    def test_archive_member_lookup_rejects_duplicate_or_missing_selected_audio(self) -> None:
        selected = [module.Candidate(10, 0, "major")]
        with tempfile.TemporaryDirectory() as directory:
            archive_path = Path(directory) / "fixture.zip"
            with zipfile.ZipFile(archive_path, "w") as archive:
                archive.writestr("a/000010.mp3", b"a")
                archive.writestr("b/000010.mp3", b"b")
            with zipfile.ZipFile(archive_path) as archive:
                with self.assertRaises(module.shared.ValidationError):
                    module._archive_members(archive, selected)

    def test_comparison_enforces_accuracy_safety_and_resource_gates(self) -> None:
        def report(correct_count: int) -> dict[str, object]:
            tracks = []
            for index in range(module.TRACK_COUNT):
                expected_tonic = index % 12
                expected_mode = "major" if index < 48 else "minor"
                correct = index < correct_count
                tracks.append(
                    {
                        "track": f"track-{index:03d}",
                        "expected_tonic": expected_tonic,
                        "expected_mode": expected_mode,
                        "key_tonic": expected_tonic if correct else (expected_tonic + 1) % 12,
                        "key_mode": expected_mode,
                        "key_confidence": 50,
                        "key_correct": correct,
                    }
                )
            return {
                "format": module.REPORT_FORMAT,
                "overall": {
                    "track_count": module.TRACK_COUNT,
                    "key_correct": correct_count,
                    "key_accuracy": correct_count / module.TRACK_COUNT,
                },
                "tracks": tracks,
                "by_mode": module._mode_summary(tracks),
            }

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = root / "baseline.json"
            candidate = root / "candidate.json"
            output = root / "comparison.json"
            baseline.write_text(json.dumps(report(68)), encoding="utf-8")
            candidate.write_text(json.dumps(report(80)), encoding="utf-8")
            value = module.compare_reports(
                baseline,
                candidate,
                "1" * 40,
                "2" * 40,
                "APTA_ENABLE_EXPERIMENTAL_TEMPORAL_CHORD_KEY=ON",
                True,
                True,
                True,
                100,
                112,
                0,
                0,
                output,
            )
            self.assertTrue(value["holdout_eligible"])
            self.assertEqual(value["fix_count"], 12)
            self.assertEqual(value["break_count"], 0)
            value = module.compare_reports(
                baseline,
                candidate,
                "1" * 40,
                "2" * 40,
                "APTA_ENABLE_EXPERIMENTAL_TEMPORAL_CHORD_KEY=ON",
                True,
                True,
                True,
                100,
                129,
                0,
                0,
                output,
            )
            self.assertFalse(value["holdout_eligible"])
            self.assertFalse(value["gates"]["workspace_delta_within_128_bytes"])


if __name__ == "__main__":
    unittest.main()
