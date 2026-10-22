#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import importlib.util
import io
import sys
import tarfile
import tempfile
import unittest
from unittest import mock
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


class DevelopmentPreparationTests(unittest.TestCase):
    def test_prepare_only_development_and_preserve_legacy_default(self):
        for development_only in (True, False):
            with self.subTest(development_only=development_only), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                rows = [SelectionTests().row(index, meter) for meter in (3, 4) for index in range(40)]
                rows = [module.Candidate(root / "source/BallroomData" / row.audio_path,
                         root / "source" / row.annotation_path, row.genre, row.meter_numerator,
                         row.beat_times, row.beat_positions) for row in rows]
                for row in rows:
                    row.audio_path.parent.mkdir(parents=True, exist_ok=True)
                    row.audio_path.write_bytes(b"fixture source")
                    row.annotation_path.write_bytes(b"fixture annotation")
                selected = module.select_candidates(rows)
                def convert(_ffmpeg, source, target):
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(source.name.encode())
                with mock.patch.object(module, "require_archive"), mock.patch.object(module, "ffmpeg_version", return_value="fixture"), \
                     mock.patch.object(module, "inventory", return_value=rows) as inventory, \
                     mock.patch.object(module, "materialize_development") as materialize, \
                     mock.patch.object(module, "canonicalize", side_effect=convert) as canonicalize:
                    manifest = module.prepare(root / "source", Path(__file__), Path(__file__), Path(__file__),
                                              root / "prepared", development_only=development_only)
                self.assertEqual(manifest["track_count"], 40 if development_only else 80)
                expected = selected["development"] if development_only else selected["development"] + selected["holdout"]
                self.assertEqual([call.args[1] for call in canonicalize.call_args_list], [row.audio_path for row in expected])
                if development_only:
                    inventory.assert_called_once_with(root / "source", Path(__file__))
                    materialize.assert_called_once_with(root / "source", Path(__file__), selected["development"])
                    self.assertEqual(manifest["prepared_splits"], ["development"])
                    self.assertIs(manifest["holdout_performances_opened"], False)
                    self.assertEqual(set(manifest["split_counts"]), {"development"})
                else:
                    inventory.assert_called_once_with(root / "source")
                    materialize.assert_not_called()
                    self.assertNotIn("prepared_splits", manifest)
                    self.assertEqual(set(manifest["split_counts"]), {"development", "holdout"})

    def test_refuses_nonempty_output_before_source_reads(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "existing").write_text("keep")
            with mock.patch.object(module, "require_archive") as read:
                with self.assertRaises(module.common.ValidationError):
                    module.prepare(root, root, root, root, root, development_only=True)
                read.assert_not_called()

    def test_materializes_selected_member_only_and_refuses_reuse(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / "fixture.tar.gz"
            with tarfile.open(archive, "w:gz") as target:
                for name in ("development", "holdout"):
                    entry = tarfile.TarInfo(f"./BallroomData/Waltz/{name}.wav")
                    payload = name.encode()
                    entry.size = len(payload)
                    target.addfile(entry, io.BytesIO(payload))
            row = module.Candidate(root / "source/BallroomData/Waltz/development.wav", root / "unused", "Waltz", 3, (), ())
            original = tarfile.TarFile.extractfile
            opened = []
            def extract(archive, member):
                opened.append(member.name)
                return original(archive, member)
            with mock.patch.object(tarfile.TarFile, "extractfile", extract):
                module.materialize_development(root / "source", archive, [row])
            self.assertEqual(opened, ["./BallroomData/Waltz/development.wav"])
            self.assertEqual(list((root / "source").rglob("*.wav")), [row.audio_path])
            self.assertEqual(row.audio_path.read_bytes(), b"development")
            with self.assertRaises(module.common.ValidationError):
                module.materialize_development(root / "source", archive, [row])

    def test_archive_rejects_unsafe_duplicate_and_link_members(self):
        for name, link in (("../outside.wav", False), ("/absolute.wav", False), ("BallroomData/Waltz/link.wav", True)):
            member = tarfile.TarInfo(name)
            if link:
                member.type = tarfile.SYMTYPE
            with self.assertRaises(module.common.ValidationError):
                module._audio_member_name(member)
        member = tarfile.TarInfo("BallroomData/Waltz/duplicate.wav")
        with self.assertRaises(module.common.ValidationError):
            module._audio_members([member, member])


if __name__ == "__main__":
    unittest.main()
