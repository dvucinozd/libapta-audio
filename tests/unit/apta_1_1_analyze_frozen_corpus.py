#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import csv
import importlib.util
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parents[2] / "tools"
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))
MODULE_PATH = TOOLS_DIR / "apta_1_1_analyze_frozen_corpus.py"
spec = importlib.util.spec_from_file_location("apta_1_1_corpus_runner", MODULE_PATH)
assert spec is not None and spec.loader is not None
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)

freezer_spec = importlib.util.spec_from_file_location(
    "apta_1_1_freezer", TOOLS_DIR / "apta_1_1_freeze_corpus.py"
)
assert freezer_spec is not None and freezer_spec.loader is not None
freezer = importlib.util.module_from_spec(freezer_spec)
freezer_spec.loader.exec_module(freezer)


class FrozenCorpusRunnerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="apta-runner-test-")
        self.root = Path(self.temporary.name)
        self.previous_cwd = Path.cwd()
        os.chdir(self.root)
        self.corpus = self.root / "private-corpus"
        self.corpus.mkdir()
        self.audio = self.corpus / "Artist - Secret Title.wav"
        self.audio.write_bytes(b"synthetic-wave-payload")
        self.track = freezer.opaque_id(freezer.sha256_file(self.audio))
        self.staging = self.root / "staging.csv"
        with self.staging.open("w", encoding="utf-8", newline="") as target:
            writer = csv.writer(target, lineterminator="\n")
            writer.writerow(["source"])
            writer.writerow([self.audio.name])
        self.manifest = self.root / "manifest.json"
        self.write_manifest([self.track])
        self.work = self.root / "publishable"
        self.output_dir = self.work / "analyzed"
        self.mapping = self.work / "mapping.csv"
        self.run_metadata = self.work / "run.json"
        self.counter = self.root / "counter.txt"
        self.analyzer = self.root / "fake_analyzer.py"
        self.write_analyzer()
        self.revision = "a" * 40

    def tearDown(self) -> None:
        os.chdir(self.previous_cwd)
        self.temporary.cleanup()

    def write_manifest(self, tracks: list[str]) -> None:
        self.manifest.write_text(
            json.dumps(
                {
                    "format": freezer.FORMAT,
                    "track_count": len(tracks),
                    "track_ids": sorted(tracks),
                },
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
        )

    def write_analyzer(self, *, fail: bool = False) -> None:
        body = (
            "#!/usr/bin/env python3\n"
            "import pathlib, sys\n"
            f"counter = pathlib.Path({str(self.counter)!r})\n"
            "count = int(counter.read_text()) if counter.exists() else 0\n"
            "counter.write_text(str(count + 1))\n"
            "args = sys.argv[1:]\n"
            "source = args[0]\n"
            "assert '/' not in source and '\\\\' not in source\n"
            "assert source.startswith('track-') and source.endswith('.wav')\n"
            "assert pathlib.Path(source).is_file()\n"
            "assert args[-2:] == ['--features', 'all']\n"
        )
        if fail:
            body += "sys.exit(7)\n"
        else:
            body += (
                "out = pathlib.Path(args[args.index('--output') + 1])\n"
                "out.parent.mkdir(parents=True, exist_ok=True)\n"
                "out.write_bytes(('APTA:' + source).encode())\n"
            )
        self.analyzer.write_text(body, encoding="utf-8")

    def run_once(self, *, resume: bool = False, revision: str | None = None) -> int:
        return runner.analyze_frozen_corpus(
            corpus_root=self.corpus,
            staging_csv=self.staging,
            manifest_path=self.manifest,
            analyzer=self.analyzer,
            output_dir=self.output_dir,
            mapping_output=self.mapping,
            run_metadata_output=self.run_metadata,
            source_revision=revision or self.revision,
            resume=resume,
        )

    def test_analyzes_with_opaque_input_and_publishable_outputs(self) -> None:
        self.assertEqual(self.run_once(), 1)
        apta = self.output_dir / f"{self.track}.apta"
        self.assertTrue(apta.is_file())
        self.assertNotIn(self.audio.name, apta.read_text(encoding="utf-8"))

        mapping_text = self.mapping.read_text(encoding="utf-8")
        metadata_text = self.run_metadata.read_text(encoding="utf-8")
        self.assertNotIn(self.audio.name, mapping_text)
        self.assertNotIn(self.audio.name, metadata_text)
        self.assertNotIn(str(self.root), mapping_text)
        self.assertNotIn(str(self.root), metadata_text)

        with self.mapping.open("r", encoding="utf-8", newline="") as source:
            rows = list(csv.DictReader(source))
        self.assertEqual(rows, [{
            "track": self.track,
            "path": f"publishable/analyzed/{self.track}.apta",
        }])

        state = json.loads(metadata_text)
        self.assertEqual(state["format"], runner.RUN_FORMAT)
        self.assertEqual(state["source_revision"], self.revision)
        self.assertEqual(state["features"], "all")
        self.assertEqual(state["track_count"], 1)
        self.assertEqual(state["completed_track_count"], 1)
        self.assertTrue(state["complete"])
        self.assertEqual(state["outputs"][0]["track"], self.track)
        self.assertEqual(len(state["outputs"][0]["apta_sha256"]), 64)
        self.assertEqual(len(state["analyzer_sha256"]), 64)
        self.assertEqual(len(state["manifest_sha256"]), 64)
        self.assertEqual(len(state["mapping_sha256"]), 64)

    def test_resume_reuses_only_matching_verified_output(self) -> None:
        self.run_once()
        self.assertEqual(self.counter.read_text(), "1")
        self.run_once(resume=True)
        self.assertEqual(self.counter.read_text(), "1")

        self.run_once(resume=True, revision="b" * 40)
        self.assertEqual(self.counter.read_text(), "2")

    def test_modified_output_is_not_reused(self) -> None:
        self.run_once()
        apta = self.output_dir / f"{self.track}.apta"
        apta.write_bytes(b"tampered")
        self.run_once(resume=True)
        self.assertEqual(self.counter.read_text(), "2")

    def test_rejects_manifest_audio_mismatch(self) -> None:
        self.write_manifest(["track-" + "0" * 24])
        with self.assertRaises(runner.RunnerError):
            self.run_once()

    def test_rejects_path_traversal(self) -> None:
        outside = self.root / "outside.wav"
        outside.write_bytes(b"outside")
        with self.staging.open("w", encoding="utf-8", newline="") as target:
            writer = csv.writer(target, lineterminator="\n")
            writer.writerow(["source"])
            writer.writerow(["../outside.wav"])
        with self.assertRaises(runner.RunnerError):
            runner.read_sources(self.corpus, self.staging)

    def test_rejects_invalid_source_revision(self) -> None:
        with self.assertRaises(runner.RunnerError):
            self.run_once(revision="short")

    def test_rejects_output_outside_mapping_tree(self) -> None:
        with self.assertRaises(runner.RunnerError):
            runner.analyze_frozen_corpus(
                corpus_root=self.corpus,
                staging_csv=self.staging,
                manifest_path=self.manifest,
                analyzer=self.analyzer,
                output_dir=self.root.parent / (self.root.name + "-outside"),
                mapping_output=self.mapping,
                run_metadata_output=self.run_metadata,
                source_revision=self.revision,
            )

    def test_reports_analyzer_failure(self) -> None:
        self.write_analyzer(fail=True)
        with self.assertRaisesRegex(runner.RunnerError, "exit 7"):
            self.run_once()


if __name__ == "__main__":
    unittest.main()
