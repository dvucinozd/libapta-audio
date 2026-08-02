#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

import importlib.util
import struct
import sys
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).parents[2] / "tools" / "rekordbox_tempo_corpus.py"
SPEC = importlib.util.spec_from_file_location("rekordbox_tempo_corpus", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
corpus = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = corpus
SPEC.loader.exec_module(corpus)


def tag(name, header_length, header_content, payload=b""):
    length = 12 + len(header_content) + len(payload)
    assert header_length == 12 + len(header_content)
    return name + struct.pack(">II", header_length, length) + header_content + payload


def anlz(path="/Contents/Artist/Track.mp3", tempi=(12800, 12800, 12799)):
    encoded = path.encode("utf-16-be") + b"\x00\x00"
    ppth = tag(b"PPTH", 16, struct.pack(">I", len(encoded)), encoded)
    entries = b"".join(
        struct.pack(">HHI", index % 4 + 1, tempo, index * 469)
        for index, tempo in enumerate(tempi)
    )
    pqtz = tag(b"PQTZ", 24, struct.pack(">III", 0, 0x00080000, len(tempi)), entries)
    total = 28 + len(ppth) + len(pqtz)
    return b"PMAI" + struct.pack(">II", 28, total) + bytes(16) + ppth + pqtz


class AnlzParserTests(unittest.TestCase):
    def test_reads_path_and_modal_tempo(self):
        result = corpus.parse_anlz_bytes(anlz())
        self.assertEqual(result.track_path, "/Contents/Artist/Track.mp3")
        self.assertEqual(result.modal_tempo_x100, 12800)
        self.assertEqual(result.beat_count, 3)
        self.assertEqual(result.unique_tempo_count, 2)
        self.assertAlmostEqual(result.modal_share, 2 / 3)

    def test_rejects_truncated_tag(self):
        with self.assertRaisesRegex(corpus.AnlzError, "file length"):
            corpus.parse_anlz_bytes(anlz()[:-1])

    def test_rejects_paths_outside_contents(self):
        with self.assertRaisesRegex(corpus.AnlzError, "outside /Contents"):
            corpus.track_path_on_device(Path("X:/"), "/PIONEER/file.mp3")

    def test_opaque_id_is_stable_and_case_insensitive(self):
        first = corpus.opaque_track_id("/Contents/Artist/Track.mp3")
        second = corpus.opaque_track_id("/contents/artist/track.MP3")
        self.assertEqual(first, second)
        self.assertRegex(first, r"^rbx-[0-9a-f]{16}$")


class ReportTests(unittest.TestCase):
    def test_summarizes_accuracy_confidence_and_octaves(self):
        rows = [
            {
                "track": "rbx-a",
                "truth": 128.0,
                "reported": 128.0,
                "relation": "exact",
                "confidence": 80,
                "octave_error": False,
            },
            {
                "track": "rbx-b",
                "truth": 120.0,
                "reported": 60.0,
                "relation": "half-time",
                "confidence": 75,
                "octave_error": True,
            },
        ]
        result = corpus.summarize_results(rows)
        self.assertEqual(result["tracks"], 2)
        self.assertEqual(result["within_1_percent"], 1)
        self.assertEqual(result["high_confidence_errors"], 1)
        self.assertEqual(result["high_confidence_octave_errors"], 1)
        self.assertEqual(result["gates"]["75"]["correct"], 1)
        self.assertEqual(result["gates"]["75"]["wrong"], 1)

    def test_compares_fixed_and_broken_selections(self):
        baseline = [
            {"track": "a", "reported": 120.0, "relation": "OTHER"},
            {"track": "b", "reported": 128.0, "relation": "exact"},
        ]
        candidate = [
            {"track": "a", "reported": 122.0, "relation": "exact"},
            {"track": "b", "reported": 130.0, "relation": "OTHER"},
        ]
        result = corpus.compare_results(baseline, candidate)
        self.assertEqual(result["changed_selection_count"], 2)
        self.assertEqual(result["fixed_track_ids"], ["a"])
        self.assertEqual(result["broken_track_ids"], ["b"])
        self.assertEqual(result["net_exact_gain"], 0)


if __name__ == "__main__":
    unittest.main()
