#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
RELEASE_DIR = REPO_ROOT / "release"
if str(RELEASE_DIR) not in sys.path:
    sys.path.insert(0, str(RELEASE_DIR))

MODULE_PATH = RELEASE_DIR / "generate_1_1_freeze_snapshot.py"
spec = importlib.util.spec_from_file_location("apta_1_1_freeze_snapshot", MODULE_PATH)
assert spec is not None and spec.loader is not None
snapshot = importlib.util.module_from_spec(spec)
spec.loader.exec_module(snapshot)
readiness = snapshot.readiness


class FreezeSnapshotTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.git("init")
        self.git("config", "user.email", "apta-test@example.invalid")
        self.git("config", "user.name", "APTA Test")

        (self.root / "include/apta").mkdir(parents=True)
        (self.root / "release").mkdir(parents=True)
        (self.root / "VERSION").write_text(
            readiness.POLICY_DEVELOPMENT_VERSION + "\n", encoding="utf-8"
        )
        (self.root / "include/apta/apta_version.h").write_text(
            '#define APTA_PACKAGE_VERSION_STRING "1.0.1"\n', encoding="utf-8"
        )
        for relative in readiness.POLICY_REQUIRED_FREEZE_PATHS:
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(f"frozen {relative}\n", encoding="utf-8")

        blockers = []
        for blocker_id, evidence_paths in readiness.POLICY_REQUIRED_BLOCKERS.items():
            blockers.append(
                {"id": blocker_id, "status": "closed", "evidence": list(evidence_paths)}
            )
            for relative in evidence_paths:
                path = self.root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(
                    json.dumps({"blocker": blocker_id}, sort_keys=True) + "\n",
                    encoding="utf-8",
                )

        self.manifest = {
            "schema": readiness.SCHEMA,
            "development_version": readiness.POLICY_DEVELOPMENT_VERSION,
            "release_version": readiness.POLICY_RELEASE_VERSION,
            "release_tag": readiness.POLICY_RELEASE_TAG,
            "blockers": blockers,
            "required_freeze_paths": list(readiness.POLICY_REQUIRED_FREEZE_PATHS),
        }
        self.write_manifest()
        self.git("add", ".")
        self.git("commit", "-m", "freeze eligible fixture")
        self.head = self.git_output("rev-parse", "HEAD")

    def tearDown(self) -> None:
        self.temp.cleanup()

    def git(self, *args: str) -> None:
        subprocess.run(
            ["git", *args],
            cwd=self.root,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

    def git_output(self, *args: str) -> str:
        return subprocess.run(
            ["git", *args],
            cwd=self.root,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        ).stdout.strip()

    def write_manifest(self) -> None:
        (self.root / "release/1.1-readiness.json").write_text(
            json.dumps(self.manifest, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )

    def test_generates_deterministic_committed_snapshot(self) -> None:
        first = snapshot.generate(self.root, self.head)
        second = snapshot.generate(self.root, self.head)
        self.assertEqual(first, second)
        self.assertEqual(first["schema"], snapshot.SCHEMA)
        self.assertEqual(first["source_revision"], self.head)
        self.assertEqual(len(first["evidence"]), 4)
        self.assertEqual(
            len(first["freeze_inputs"]), len(readiness.POLICY_REQUIRED_FREEZE_PATHS)
        )
        for record in [first["readiness"], *first["evidence"], *first["freeze_inputs"]]:
            self.assertRegex(record["sha256"], r"^[0-9a-f]{64}$")
            self.assertRegex(record["git_blob_sha1"], r"^[0-9a-f]{40}$")

    def test_open_blocker_cannot_generate_snapshot(self) -> None:
        self.manifest["blockers"][0]["status"] = "open"
        self.write_manifest()
        with self.assertRaisesRegex(snapshot.SnapshotError, "not freeze-eligible"):
            snapshot.generate(self.root, self.head)

    def test_wrong_source_revision_is_rejected(self) -> None:
        with self.assertRaisesRegex(snapshot.SnapshotError, "does not match"):
            snapshot.generate(self.root, "0" * 40)

    def test_modified_tracked_input_is_rejected(self) -> None:
        relative = readiness.POLICY_REQUIRED_FREEZE_PATHS[0]
        (self.root / relative).write_text("locally modified\n", encoding="utf-8")
        with self.assertRaisesRegex(snapshot.SnapshotError, "tracked worktree must be clean"):
            snapshot.generate(self.root, self.head)

    def test_untracked_evidence_is_rejected(self) -> None:
        blocker = self.manifest["blockers"][0]
        old_relative = blocker["evidence"][0]
        new_relative = readiness.POLICY_REQUIRED_BLOCKERS[blocker["id"]][0]
        self.assertEqual(old_relative, new_relative)
        evidence_path = self.root / old_relative
        self.git("rm", old_relative)
        self.git("commit", "-m", "remove evidence")
        evidence_path.parent.mkdir(parents=True, exist_ok=True)
        evidence_path.write_text("{}\n", encoding="utf-8")
        head = self.git_output("rev-parse", "HEAD")
        with self.assertRaisesRegex(snapshot.SnapshotError, "not tracked at HEAD"):
            snapshot.generate(self.root, head)

    def test_existing_release_tag_blocks_snapshot(self) -> None:
        self.git("tag", readiness.POLICY_RELEASE_TAG)
        with self.assertRaisesRegex(snapshot.SnapshotError, "not freeze-eligible"):
            snapshot.generate(self.root, self.head)

    def test_snapshot_binds_readiness_manifest_itself(self) -> None:
        result = snapshot.generate(self.root, self.head)
        record = result["readiness"]
        self.assertEqual(record["path"], "release/1.1-readiness.json")
        expected = snapshot.git_output(
            self.root, "rev-parse", "HEAD:release/1.1-readiness.json"
        )
        self.assertEqual(record["git_blob_sha1"], expected)


if __name__ == "__main__":
    unittest.main()
