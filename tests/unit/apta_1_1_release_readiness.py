#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

MODULE_PATH = Path(__file__).resolve().parents[2] / "release/check_1_1_readiness.py"
spec = importlib.util.spec_from_file_location("apta_1_1_readiness", MODULE_PATH)
assert spec is not None and spec.loader is not None
readiness = importlib.util.module_from_spec(spec)
spec.loader.exec_module(readiness)


class ReadinessTests(unittest.TestCase):
    def make_root(self, version: str = "1.0.1") -> Path:
        root = Path(self.temp.name)
        (root / "include/apta").mkdir(parents=True, exist_ok=True)
        (root / "VERSION").write_text(version + "\n", encoding="utf-8")
        (root / "include/apta/apta_version.h").write_text(
            f'#define APTA_PACKAGE_VERSION_STRING "{version}"\n', encoding="utf-8"
        )
        for relative in readiness.POLICY_REQUIRED_FREEZE_PATHS:
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("frozen\n", encoding="utf-8")
        return root

    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = self.make_root()
        self.manifest = {
            "schema": readiness.SCHEMA,
            "development_version": "1.0.1",
            "release_version": "1.1.0",
            "release_tag": "v1.1.0",
            "required_freeze_paths": list(readiness.POLICY_REQUIRED_FREEZE_PATHS),
            "blockers": [
                {"id": "fresh-corpus", "status": "open", "evidence": ["evidence/fresh.json"]},
                {"id": "hardware", "status": "open", "evidence": ["evidence/hardware.json"]},
            ],
        }

    def tearDown(self) -> None:
        self.temp.cleanup()

    @mock.patch.object(readiness, "tags_at_head", return_value=set())
    def test_open_blockers_are_valid_blocked_state(self, _tags: mock.Mock) -> None:
        report = readiness.evaluate(self.root, self.manifest)
        self.assertEqual(report["status"], "pass")
        self.assertEqual(report["phase"], "blocked")
        self.assertEqual(report["open_blocker_count"], 2)

    @mock.patch.object(readiness, "tags_at_head", return_value=set())
    def test_premature_version_bump_fails(self, _tags: mock.Mock) -> None:
        (self.root / "VERSION").write_text("1.1.0\n", encoding="utf-8")
        report = readiness.evaluate(self.root, self.manifest)
        self.assertEqual(report["status"], "fail")
        self.assertTrue(any("prematurely" in item for item in report["failures"]))

    @mock.patch.object(readiness, "tags_at_head", return_value={"v1.1.0"})
    def test_premature_release_tag_fails(self, _tags: mock.Mock) -> None:
        report = readiness.evaluate(self.root, self.manifest)
        self.assertEqual(report["status"], "fail")
        self.assertTrue(any("release tag" in item for item in report["failures"]))

    @mock.patch.object(readiness, "tags_at_head", return_value=set())
    def test_closed_blocker_requires_evidence(self, _tags: mock.Mock) -> None:
        manifest = json.loads(json.dumps(self.manifest))
        manifest["blockers"][0]["status"] = "closed"
        report = readiness.evaluate(self.root, manifest)
        self.assertEqual(report["status"], "fail")
        self.assertTrue(any("missing evidence" in item for item in report["failures"]))

    @mock.patch.object(readiness, "tags_at_head", return_value=set())
    def test_all_evidence_makes_tree_freeze_eligible(self, _tags: mock.Mock) -> None:
        manifest = json.loads(json.dumps(self.manifest))
        evidence = self.root / "evidence"
        evidence.mkdir()
        for blocker in manifest["blockers"]:
            blocker["status"] = "closed"
            relative = blocker["evidence"][0]
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("{}\n", encoding="utf-8")
        report = readiness.evaluate(self.root, manifest)
        self.assertEqual(report["status"], "pass")
        self.assertEqual(report["phase"], "freeze-eligible")
        self.assertEqual(report["open_blocker_count"], 0)

    @mock.patch.object(readiness, "tags_at_head", return_value=set())
    def test_manifest_cannot_drop_policy_required_abi_surface(self, _tags: mock.Mock) -> None:
        manifest = json.loads(json.dumps(self.manifest))
        manifest["required_freeze_paths"].remove("abi/public-layout-llp64.txt")
        report = readiness.evaluate(self.root, manifest)
        self.assertEqual(report["status"], "fail")
        self.assertTrue(
            any(
                "policy-required freeze input omitted" in item
                and "public-layout-llp64.txt" in item
                for item in report["failures"]
            )
        )

    @mock.patch.object(readiness, "tags_at_head", return_value=set())
    def test_required_freeze_path_must_stay_inside_repository(self, _tags: mock.Mock) -> None:
        manifest = json.loads(json.dumps(self.manifest))
        manifest["required_freeze_paths"].append("../outside.txt")
        report = readiness.evaluate(self.root, manifest)
        self.assertEqual(report["status"], "fail")
        self.assertTrue(any("escapes repository" in item for item in report["failures"]))

    @mock.patch.object(readiness, "tags_at_head", return_value=set())
    def test_closed_evidence_path_must_stay_inside_repository(self, _tags: mock.Mock) -> None:
        manifest = json.loads(json.dumps(self.manifest))
        manifest["blockers"][0]["status"] = "closed"
        manifest["blockers"][0]["evidence"] = ["../outside.json"]
        report = readiness.evaluate(self.root, manifest)
        self.assertEqual(report["status"], "fail")
        self.assertTrue(any("evidence path escapes repository" in item for item in report["failures"]))


if __name__ == "__main__":
    unittest.main()
