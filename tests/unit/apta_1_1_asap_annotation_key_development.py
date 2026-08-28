#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path


TOOLS = Path(__file__).resolve().parents[2] / "tools"
MODULE_PATH = TOOLS / "apta_1_1_asap_annotation_key_development.py"
spec = importlib.util.spec_from_file_location("apta_asap_annotated_key", MODULE_PATH)
assert spec is not None and spec.loader is not None
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)


class KeyEventTests(unittest.TestCase):
    def test_normalizes_sorted_major_and_minor_events(self) -> None:
        self.assertEqual(
            module._key_events({"0": "Db", "12.5": "F#m"}),
            [(0.0, (1, "major")), (12.5, (6, "minor"))],
        )

    def test_rejects_duplicate_numeric_times(self) -> None:
        with self.assertRaisesRegex(module.FreezeError, "duplicate_annotation_time"):
            module._key_events({"0": "C", "0.0": "C"})

    def test_rejects_non_increasing_source_order(self) -> None:
        with self.assertRaisesRegex(
            module.FreezeError, "non_increasing_annotation_times"
        ):
            module._key_events({"2": "C", "1": "C"})

    def test_rejects_unknown_or_non_string_tokens(self) -> None:
        with self.assertRaisesRegex(module.FreezeError, "unrecognized_key_signature"):
            module._key_events({"0": "c minor"})
        with self.assertRaisesRegex(module.FreezeError, "non_string_key_signature"):
            module._key_events({"0": ["C"]})

    def test_rejects_empty_mapping(self) -> None:
        with self.assertRaisesRegex(module.FreezeError, "missing_perf_key_signatures"):
            module._key_events({})


if __name__ == "__main__":
    unittest.main()
