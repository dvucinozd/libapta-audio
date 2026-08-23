#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS))
MODULE_PATH = TOOLS / "apta_1_1_ballroom_meter_validation.py"
spec = importlib.util.spec_from_file_location("apta_ballroom_meter", MODULE_PATH)
assert spec is not None and spec.loader is not None
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
prior_asap_module = sys.modules.get("apta_1_1_asap_meter_validation")
spec.loader.exec_module(module)
if prior_asap_module is None:
    sys.modules.pop("apta_1_1_asap_meter_validation", None)
else:
    sys.modules["apta_1_1_asap_meter_validation"] = prior_asap_module


class BeatAnnotationTests(unittest.TestCase):
    def test_reads_space_and_tab_separated_rows(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "track.beats"
            rows = []
            for index in range(12):
                separator = "\t" if index % 2 else " "
                rows.append(f"{index * 0.5:.3f}{separator}{index % 3 + 1}")
            path.write_text("\n".join(rows) + "\n", encoding="utf-8")
            times, positions = module.read_beats(path)
            self.assertEqual(len(times), 12)
            self.assertEqual(set(positions), {1, 2, 3})

    def test_rejects_non_increasing_timestamps(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "track.beats"
            path.write_text("0 1\n" * 12, encoding="utf-8")
            with self.assertRaises(module.common.ValidationError):
                module.read_beats(path)

    def test_archive_checksum_is_enforced(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "archive"
            path.write_bytes(b"test")
            module.require_archive(
                path, "098f6bcd4621d373cade4e832627b4f6", "fixture"
            )
            with self.assertRaises(module.common.ValidationError):
                module.require_archive(path, "0" * 32, "fixture")


class SelectionTests(unittest.TestCase):
    def row(self, index: int, meter: int) -> object:
        genre = "Waltz" if meter == 3 else "Tango"
        positions = tuple(index % meter + 1 for index in range(24))
        return module.Candidate(
            audio_path=Path(genre) / f"track-{meter}-{index}.wav",
            annotation_path=Path(f"track-{meter}-{index}.beats"),
            genre=genre,
            meter_numerator=meter,
            beat_times=tuple(index * 0.5 for index in range(24)),
            beat_positions=positions,
        )

    def test_selection_is_balanced_deterministic_and_disjoint(self) -> None:
        rows = [self.row(index, meter) for meter in (3, 4) for index in range(12)]
        first = module.select_candidates(rows, per_meter_per_split=5)
        second = module.select_candidates(rows, per_meter_per_split=5)
        self.assertEqual(first, second)
        for split in ("development", "holdout"):
            self.assertEqual(len(first[split]), 10)
            self.assertEqual(sum(row.meter_numerator == 3 for row in first[split]), 5)
            self.assertEqual(sum(row.meter_numerator == 4 for row in first[split]), 5)
        self.assertFalse(
            {row.audio_path.stem for row in first["development"]}
            & {row.audio_path.stem for row in first["holdout"]}
        )


if __name__ == "__main__":
    unittest.main()
