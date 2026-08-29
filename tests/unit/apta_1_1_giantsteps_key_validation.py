#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[2] / "tools"
MODULE_PATH = TOOLS / "apta_1_1_giantsteps_key_validation.py"
spec = importlib.util.spec_from_file_location("apta_giantsteps_key", MODULE_PATH)
assert spec is not None and spec.loader is not None
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)


class AnnotationTests(unittest.TestCase):
    def test_reads_key_and_confidence_without_comment(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "track.key"
            path.write_text("C# minor\t2\tcomment\n", encoding="utf-8")
            self.assertEqual(module.read_key_annotation(path), ("c# minor", "2"))

    def test_rejects_missing_confidence(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "track.key"
            path.write_text("c major\n", encoding="utf-8")
            with self.assertRaises(module.ValidationError):
                module.read_key_annotation(path)


class SelectionTests(unittest.TestCase):
    @staticmethod
    def rows() -> list[object]:
        rows = []
        for key_name in module.KEY_NAMES:
            tonic, mode = key_name.split()
            for index in range(8):
                rows.append(
                    module.Candidate(
                        source_id=f"{tonic}-{mode}-{index}.LOFI",
                        key_name=key_name,
                        key_tonic=module.TONICS[tonic],
                        key_mode=mode,
                        transport_md5=f"{index:032x}",
                    )
                )
        return rows

    def test_selection_is_balanced_deterministic_and_disjoint(self) -> None:
        first = module.select_candidates(self.rows())
        second = module.select_candidates(reversed(self.rows()))
        self.assertEqual(first, second)
        self.assertEqual(len(first["development"]), 96)
        self.assertEqual(len(first["holdout"]), 48)
        self.assertFalse(
            {row.source_id for row in first["development"]}
            & {row.source_id for row in first["holdout"]}
        )
        for split, count in (("development", 4), ("holdout", 2)):
            for key_name in module.KEY_NAMES:
                self.assertEqual(
                    sum(row.key_name == key_name for row in first[split]), count
                )

    def test_selection_seal_changes_when_a_label_changes(self) -> None:
        first = module.select_candidates(self.rows())
        changed = {name: list(rows) for name, rows in first.items()}
        row = changed["development"][0]
        changed["development"][0] = module.Candidate(
            source_id=row.source_id,
            key_name="b minor",
            key_tonic=11,
            key_mode="minor",
            transport_md5=row.transport_md5,
        )
        self.assertNotEqual(module.selection_sha256(first), module.selection_sha256(changed))


class GateTests(unittest.TestCase):
    def test_holdout_requires_explicit_open_and_revision(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaisesRegex(module.ValidationError, "open-holdout"):
                module.prepare(
                    root,
                    root,
                    root / "out",
                    "holdout",
                    "ffmpeg",
                    "curl",
                    "2026-01-01T00:00:00Z",
                )
            with self.assertRaisesRegex(module.ValidationError, "candidate-revision"):
                module.prepare(
                    root,
                    root,
                    root / "out",
                    "holdout",
                    "ffmpeg",
                    "curl",
                    "2026-01-01T00:00:00Z",
                    open_holdout=True,
                )

    def test_scores_exact_and_high_confidence_errors(self) -> None:
        labels = [
            {"track": "track-a", "key_tonic": 0, "key_mode": "major"},
            {"track": "track-b", "key_tonic": 9, "key_mode": "minor"},
        ]
        results = [
            {"track": "track-a", "key_tonic": 0, "key_mode": "major", "key_confidence": 80},
            {"track": "track-b", "key_tonic": 0, "key_mode": "major", "key_confidence": 90},
        ]
        score = module.score_rows(labels, results)
        self.assertEqual(score["overall"]["key_correct"], 1)
        self.assertEqual(score["overall"]["high_confidence_key_errors"], 1)
        self.assertEqual(score["tracks"][1]["error_family"], "relative")

    def test_comparison_fails_holdout_gate_below_seventy_percent(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            tracks = []
            for index in range(10):
                tracks.append(
                    {
                        "track": f"track-{index}",
                        "key_tonic": index % 12,
                        "key_mode": "major",
                        "key_confidence": 50,
                        "key_correct": index < 6,
                    }
                )
            baseline = {
                "format": module.REPORT_FORMAT,
                "split": "development",
                "overall": {"track_count": 10, "key_correct": 5, "key_accuracy": 0.5},
                "tracks": [
                    {
                        **row,
                        "expected_tonic": row["key_tonic"] if index < 5 else (row["key_tonic"] + 1) % 12,
                        "expected_mode": "major",
                        "key_correct": index < 5,
                    }
                    for index, row in enumerate(tracks)
                ],
            }
            candidate = {
                "format": module.REPORT_FORMAT,
                "split": "development",
                "overall": {"track_count": 10, "key_correct": 6, "key_accuracy": 0.6},
                "tracks": [
                    {
                        **row,
                        "expected_tonic": row["key_tonic"] if index < 6 else (row["key_tonic"] + 1) % 12,
                        "expected_mode": "major",
                    }
                    for index, row in enumerate(tracks)
                ],
            }
            baseline_path = root / "baseline.json"
            candidate_path = root / "candidate.json"
            baseline_path.write_text(json.dumps(baseline), encoding="utf-8")
            candidate_path.write_text(json.dumps(candidate), encoding="utf-8")
            report = module.compare_reports(
                baseline_path,
                candidate_path,
                "1" * 40,
                "2" * 40,
                "APTA_ENABLE_EXPERIMENTAL_HARMONIC_HPCP=ON",
                root / "comparison.json",
            )
            self.assertFalse(report["holdout_eligible"])
            self.assertFalse(report["gates"]["development_accuracy_at_least_70_percent"])

    def test_comparison_rejects_inconsistent_report(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            report = {
                "format": module.REPORT_FORMAT,
                "split": "development",
                "overall": {"track_count": 1, "key_correct": 1, "key_accuracy": 1.0},
                "tracks": [
                    {
                        "track": "track-a",
                        "expected_tonic": 0,
                        "expected_mode": "major",
                        "key_tonic": 1,
                        "key_mode": "major",
                        "key_confidence": 50,
                        "key_correct": True,
                    }
                ],
            }
            path = root / "report.json"
            path.write_text(json.dumps(report), encoding="utf-8")
            with self.assertRaisesRegex(module.ValidationError, "correctness"):
                module._load_report(path)


if __name__ == "__main__":
    unittest.main()
