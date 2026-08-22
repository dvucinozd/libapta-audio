#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import importlib.util
import os
import tempfile
import unittest
from pathlib import Path

MODULE_PATH = Path(__file__).resolve().parents[2] / "tools/apta_1_1_prepare_corpus.py"
spec = importlib.util.spec_from_file_location("apta_1_1_prepare_corpus", MODULE_PATH)
assert spec is not None and spec.loader is not None
prep = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prep)

ROOT = Path(__file__).resolve().parents[2]
LABEL_HTML = ROOT / "tools/apta_1_1_label_corpus.html"


class CorpusPreparationTests(unittest.TestCase):
    def test_ffmpeg_command_freezes_canonical_format(self) -> None:
        command = prep.ffmpeg_command(Path("ffmpeg"), Path("in.flac"), Path("out.wav"))
        joined = " ".join(command)
        self.assertIn("-map_metadata -1", joined)
        self.assertIn("-fflags +bitexact", joined)
        self.assertIn("-flags:a +bitexact", joined)
        self.assertIn("-ar 48000", joined)
        self.assertIn("-ac 2", joined)
        self.assertIn("-c:a pcm_s16le", joined)
        self.assertEqual(command[-1], "out.wav")

    def test_discovers_only_supported_audio(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ("b.MP3", "a.flac", "nested/c.wav", "ignore.txt"):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"x")
            found = [path.relative_to(root).as_posix() for path in prep.discover_sources(root)]
            self.assertEqual(found, ["a.flac", "b.MP3", "nested/c.wav"])

    def test_rejects_extension_collision(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            output = root / "canonical"
            source.mkdir()
            (source / "same.mp3").write_bytes(b"a")
            (source / "same.flac").write_bytes(b"b")
            fake = self._fake_ffmpeg(root)
            with self.assertRaises(prep.PreparationError):
                prep.prepare(source, output, root / "manifest.json", fake)

    def test_prepare_records_hashes_and_canonical_wav(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            output = root / "canonical"
            source.mkdir()
            (source / "Artist - Private Song.mp3").write_bytes(b"private compressed source")
            fake = self._fake_ffmpeg(root)
            manifest_path = root / "private-preparation.json"
            manifest = prep.prepare(source, output, manifest_path, fake)
            self.assertTrue(manifest["local_only"])
            self.assertEqual(manifest["track_count"], 1)
            self.assertEqual(manifest["canonical_audio"]["sample_rate"], 48000)
            self.assertEqual(manifest["canonical_audio"]["channels"], 2)
            row = manifest["tracks"][0]
            self.assertEqual(row["source"], "Artist - Private Song.mp3")
            self.assertEqual(row["canonical"], "Artist - Private Song.wav")
            self.assertEqual(len(row["source_sha256"]), 64)
            self.assertEqual(len(row["canonical_sha256"]), 64)
            self.assertEqual(row["sample_rate"], 48000)
            self.assertGreater(row["frames"], 0)
            self.assertTrue((output / "Artist - Private Song.wav").is_file())

    def test_label_helper_is_local_and_exports_frozen_staging_fields(self) -> None:
        text = LABEL_HTML.read_text(encoding="utf-8")
        for field in (
            "source",
            "key_tonic",
            "key_mode",
            "meter_numerator",
            "meter_denominator",
            "downbeat_frame",
            "beat_period_frames",
        ):
            self.assertIn(field, text)
        self.assertIn("parseWav", text)
        self.assertIn("Mark current as downbeat", text)
        self.assertIn("Mark current as reference beat", text)
        self.assertIn('accept="audio/wav,.wav"', text)
        self.assertNotIn("http://", text)
        self.assertNotIn("https://", text)

    @staticmethod
    def _fake_ffmpeg(root: Path) -> Path:
        script = root / "fake_ffmpeg.py"
        script.write_text(
            "#!/usr/bin/env python3\n"
            "import pathlib, sys, wave\n"
            "if '-version' in sys.argv:\n"
            "    print('ffmpeg version fake-apta-test')\n"
            "    raise SystemExit(0)\n"
            "out = pathlib.Path(sys.argv[-1])\n"
            "out.parent.mkdir(parents=True, exist_ok=True)\n"
            "with wave.open(str(out), 'wb') as w:\n"
            "    w.setnchannels(2)\n"
            "    w.setsampwidth(2)\n"
            "    w.setframerate(48000)\n"
            "    w.writeframes(b'\\x00\\x00\\x00\\x00' * 256)\n",
            encoding="utf-8",
        )
        os.chmod(script, 0o755)
        return script


if __name__ == "__main__":
    unittest.main()
