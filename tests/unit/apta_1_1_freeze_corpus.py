#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import csv
import importlib.util
import tempfile
import unittest
from pathlib import Path

MODULE_PATH = Path(__file__).resolve().parents[2] / "tools/apta_1_1_freeze_corpus.py"
spec = importlib.util.spec_from_file_location("apta_1_1_freeze_corpus", MODULE_PATH)
assert spec is not None and spec.loader is not None
freeze_mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(freeze_mod)


class CorpusFreezeTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.corpus = self.root / "audio"
        self.corpus.mkdir()

    def tearDown(self) -> None:
        self.temp.cleanup()

    def staging(self, count: int, duplicate: bool = False) -> Path:
        path = self.root / "staging.csv"
        with path.open("w", encoding="utf-8", newline="") as target:
            writer = csv.DictWriter(target, fieldnames=freeze_mod.STAGING_FIELDS, lineterminator="\n")
            writer.writeheader()
            for index in range(count):
                name = f"Artist - Secret Title {index}.wav"
                payload = b"same" if duplicate else f"audio-{index}".encode()
                (self.corpus / name).write_bytes(payload)
                writer.writerow({
                    "source": name,
                    "key_tonic": index % 12,
                    "key_mode": "major" if index % 2 == 0 else "minor",
                    "meter_numerator": 4,
                    "meter_denominator": 4,
                    "downbeat_frame": index * 1000,
                    "beat_period_frames": 24000,
                })
        return path

    def run_freeze(self, count: int, allow_diagnostic: bool = False) -> tuple[Path, Path, dict[str, object]]:
        labels = self.root / "labels.csv"
        manifest = self.root / "manifest.json"
        report = freeze_mod.freeze(
            self.corpus,
            self.staging(count),
            labels,
            manifest,
            "2026-08-22T00:00:00Z",
            "manual-reference",
            "two-person manual verification",
            allow_diagnostic,
        )
        return labels, manifest, report

    def test_sanitizes_local_titles_and_paths(self) -> None:
        labels, manifest, report = self.run_freeze(3, allow_diagnostic=True)
        labels_text = labels.read_text(encoding="utf-8")
        manifest_text = manifest.read_text(encoding="utf-8")
        self.assertNotIn("Artist", labels_text)
        self.assertNotIn("Secret Title", labels_text)
        self.assertNotIn(".wav", labels_text)
        self.assertNotIn("Artist", manifest_text)
        self.assertEqual(report["track_count"], 3)
        self.assertTrue(all(str(item).startswith("track-") for item in report["track_ids"]))
        self.assertEqual(report["track_ids"], sorted(report["track_ids"]))

    def test_rejects_undersized_acceptance_corpus(self) -> None:
        with self.assertRaises(freeze_mod.FreezeError):
            self.run_freeze(47, allow_diagnostic=False)

    def test_accepts_minimum_corpus(self) -> None:
        _labels, _manifest, report = self.run_freeze(48)
        self.assertEqual(report["track_count"], 48)
        self.assertEqual(len(report["labels_sha256"]), 64)

    def test_rejects_duplicate_audio_content(self) -> None:
        labels = self.root / "labels.csv"
        manifest = self.root / "manifest.json"
        with self.assertRaises(freeze_mod.FreezeError):
            freeze_mod.freeze(
                self.corpus,
                self.staging(2, duplicate=True),
                labels,
                manifest,
                "2026-08-22T00:00:00Z",
                "manual-reference",
                "manual verification",
                True,
            )

    def test_rejects_path_escape(self) -> None:
        outside = self.root / "outside.wav"
        outside.write_bytes(b"outside")
        staging = self.root / "escape.csv"
        with staging.open("w", encoding="utf-8", newline="") as target:
            writer = csv.DictWriter(target, fieldnames=freeze_mod.STAGING_FIELDS, lineterminator="\n")
            writer.writeheader()
            writer.writerow({
                "source": "../outside.wav",
                "key_tonic": 0,
                "key_mode": "major",
                "meter_numerator": 4,
                "meter_denominator": 4,
                "downbeat_frame": 0,
                "beat_period_frames": 24000,
            })
        with self.assertRaises(freeze_mod.FreezeError):
            freeze_mod.freeze(
                self.corpus,
                staging,
                self.root / "labels.csv",
                self.root / "manifest.json",
                "2026-08-22T00:00:00Z",
                "manual-reference",
                "manual verification",
                True,
            )


if __name__ == "__main__":
    unittest.main()
