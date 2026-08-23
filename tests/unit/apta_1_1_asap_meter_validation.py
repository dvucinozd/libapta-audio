#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

TOOLS = Path(__file__).resolve().parents[2] / "tools"
MODULE_PATH = TOOLS / "apta_1_1_asap_meter_validation.py"
spec = importlib.util.spec_from_file_location("apta_asap_meter", MODULE_PATH)
assert spec is not None and spec.loader is not None
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)


def annotation(meter: str, offset: float = 0.0) -> dict[str, object]:
    numerator = int(meter[0])
    beats = [offset + index * 0.5 for index in range(100)]
    downbeats = beats[::numerator]
    return {
        "performance_beats": beats,
        "performance_downbeats": downbeats,
        "perf_time_signatures": {str(offset): [meter, numerator]},
        "score_and_performance_aligned": True,
    }


class SelectionTests(unittest.TestCase):
    def test_selection_is_deterministic_balanced_and_score_disjoint(self) -> None:
        annotations: dict[str, dict[str, object]] = {}
        for meter in ("3/4", "4/4"):
            for score in range(6):
                for performance in range(2):
                    path = f"Composer/{meter.replace('/', '-')}-{score}/p{performance}.mid"
                    annotations[path] = annotation(meter, performance * 0.01)
        first = module.select_candidates(annotations, per_meter_per_split=3)
        second = module.select_candidates(annotations, per_meter_per_split=3)
        self.assertEqual(first, second)
        for split in ("development", "holdout"):
            self.assertEqual(len(first[split]), 6)
            self.assertEqual(
                {meter: sum(row.meter == meter for row in first[split]) for meter in ("3/4", "4/4")},
                {"3/4": 3, "4/4": 3},
            )
        self.assertFalse(
            {row.score_id for row in first["development"]}
            & {row.score_id for row in first["holdout"]}
        )

    def test_rejects_insufficient_distinct_scores(self) -> None:
        annotations = {
            "Composer/only-score/performance.mid": annotation("3/4"),
            "Composer/common-time/performance.mid": annotation("4/4"),
        }
        with self.assertRaises(module.ValidationError):
            module.select_candidates(annotations, per_meter_per_split=1)


class ScoringTests(unittest.TestCase):
    def labels(self) -> list[dict[str, object]]:
        return [
            {
                "track": "track-a",
                "split": "development",
                "meter_numerator": 3,
                "meter_denominator": 4,
                "beat_frames": [0, 24000, 48000, 72000, 96000, 120000],
                "downbeat_frames": [0, 72000],
            },
            {
                "track": "track-b",
                "split": "development",
                "meter_numerator": 4,
                "meter_denominator": 4,
                "beat_frames": [0, 20000, 40000, 60000, 80000, 100000],
                "downbeat_frames": [0, 80000],
            },
        ]

    def test_scores_meter_and_nearest_annotated_downbeat(self) -> None:
        results = [
            {
                "track": "track-a",
                "meter_numerator": 3,
                "meter_denominator": 4,
                "meter_confidence": 80,
                "downbeat_frame": 71900,
                "downbeat_confidence": 80,
                "beat_period_frames": 24000,
            },
            {
                "track": "track-b",
                "meter_numerator": 3,
                "meter_denominator": 4,
                "meter_confidence": 90,
                "downbeat_frame": 70000,
                "downbeat_confidence": 90,
                "beat_period_frames": 19000,
            },
        ]
        report = module.score_rows(self.labels(), results)
        self.assertEqual(report["overall"]["meter_correct"], 1)
        self.assertEqual(report["overall"]["downbeat_correct"], 1)
        self.assertEqual(report["overall"]["high_confidence_meter_errors"], 1)
        self.assertEqual(report["overall"]["high_confidence_downbeat_errors"], 1)
        self.assertEqual(report["overall"]["period_within_1_percent"], 1)
        self.assertEqual(report["overall"]["period_within_10_percent"], 2)
        self.assertEqual(report["by_meter"]["3/4"]["meter_correct"], 1)
        self.assertEqual(report["by_meter"]["4/4"]["meter_correct"], 0)

    def test_requires_exact_track_set(self) -> None:
        with self.assertRaises(module.ValidationError):
            module.score_rows(self.labels(), [])


class PreparedCorpusTests(unittest.TestCase):
    def test_labels_are_bound_to_manifest_hash(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            labels = [
                {
                    "track": "track-a",
                    "split": "development",
                    "meter_numerator": 3,
                    "meter_denominator": 4,
                    "beat_frames": [0, 100],
                    "downbeat_frames": [0],
                }
            ]
            label_text = json.dumps(labels, indent=2, sort_keys=True) + "\n"
            (root / "labels.json").write_text(label_text, encoding="utf-8")
            manifest = {
                "format": module.FORMAT,
                "track_ids": ["track-a"],
                "labels_sha256": module.sha256_file(root / "labels.json"),
            }
            (root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            _manifest, loaded = module.load_prepared(root)
            self.assertEqual(loaded, labels)
            (root / "labels.json").write_text(label_text + " ", encoding="utf-8")
            with self.assertRaises(module.ValidationError):
                module.load_prepared(root)


class RunnerTests(unittest.TestCase):
    def test_existing_output_is_reanalyzed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            prepared = root / "prepared"
            audio_dir = prepared / "audio"
            audio_dir.mkdir(parents=True)
            audio_bytes = b"deterministic test audio"
            audio_hash = module.hashlib.sha256(audio_bytes).hexdigest()
            track = f"track-{audio_hash[:24]}"
            (audio_dir / f"{track}.wav").write_bytes(audio_bytes)
            labels = [
                {
                    "track": track,
                    "split": "development",
                    "meter_numerator": 3,
                    "meter_denominator": 4,
                    "beat_frames": [0, 100],
                    "downbeat_frames": [0],
                }
            ]
            (prepared / "labels.json").write_text(
                json.dumps(labels, indent=2, sort_keys=True) + "\n", encoding="utf-8"
            )
            manifest = {
                "format": module.FORMAT,
                "track_ids": [track],
                "labels_sha256": module.sha256_file(prepared / "labels.json"),
            }
            (prepared / "manifest.json").write_text(
                json.dumps(manifest), encoding="utf-8"
            )
            analyzer = root / "apta-analyze"
            analyzer.write_bytes(b"fake analyzer")
            output = root / "output"
            stale = output / "analyzed" / f"{track}.apta"
            stale.parent.mkdir(parents=True)
            stale.write_bytes(b"stale")

            def analyze(command: list[str], check: bool) -> None:
                self.assertTrue(check)
                target = Path(command[command.index("--output") + 1])
                target.write_bytes(b"fresh")

            with mock.patch.object(module.subprocess, "run", side_effect=analyze) as run:
                result = module.run_analysis(
                    prepared,
                    analyzer,
                    output,
                    "development",
                    "a" * 40,
                )

            run.assert_called_once()
            self.assertEqual(stale.read_bytes(), b"fresh")
            self.assertEqual(result["outputs"][0]["apta_sha256"], module.sha256_file(stale))


if __name__ == "__main__":
    unittest.main()
