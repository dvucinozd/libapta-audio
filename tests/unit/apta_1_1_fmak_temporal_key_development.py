#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import importlib.util
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


if __name__ == "__main__":
    unittest.main()
