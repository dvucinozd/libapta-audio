# SPDX-License-Identifier: Apache-2.0
import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import apta_key_disagreement_topology as topology


def fixture():
    reports = {"baseline": {"tracks": []}, "essentia": {"rows": []},
               "openkeyscan": {"rows": []}, "coordinator": {"private_rows": []}, "review": {"rows": []}}
    for i in range(72):
        track = f"track-{i:024x}"
        e = (0, "major") if i < 50 else (9, "minor")
        common = dict(track=track, expected_tonic=0, expected_mode="major")
        reports["baseline"]["tracks"].append(dict(common, key_tonic=0, key_mode="major", key_correct=True))
        reports["essentia"]["rows"].append(dict(common, key_tonic=e[0], key_mode=e[1], correct=i < 50))
        reports["openkeyscan"]["rows"].append(dict(track=track, key_tonic=0, key_mode="major",
            agrees_apta=True, agrees_essentia=i < 50, agrees_fmak=True, apta_agrees_essentia=i < 50,
            all_three_algorithms_agree=i < 50, canonical_source_unchanged=True, disposable_pcm_identical=True))
        if i < 12:
            sample = f"A{i + 1:02d}"
            reports["coordinator"]["private_rows"].append(dict(track=track, sample=sample,
                baseline=[0, "major"], reference=list(e), key_tonic=0, key_mode="major"))
            reports["review"]["rows"].append(dict(sample=sample, listener1=[0, "major"],
                listener2=[0, "major"] if i < 9 else None, openkeyscan=[0, "major"]))
    return reports


class TopologyTests(unittest.TestCase):
    def test_all_576_pairs_symmetric_and_transposition_invariant(self):
        keys = [(tonic, mode) for tonic in range(12) for mode in ("major", "minor")]
        counts = {name: 0 for name in topology.FAMILIES}
        for a in keys:
            for b in keys:
                result = topology.relation(a, b)
                counts[result] += 1
                self.assertEqual(result, topology.relation(b, a))
                for shift in range(12):
                    self.assertEqual(result, topology.relation(((a[0] + shift) % 12, a[1]),
                                                               ((b[0] + shift) % 12, b[1])))
        self.assertEqual(counts, dict(exact=24, parallel_mode=24, relative_major_minor=24,
                                     same_mode_fourth_fifth=48, other_same_mode_tonic=216, other_cross_mode=240))

    def test_relative_direction_and_nonrelative_thirds(self):
        self.assertEqual(topology.relation((0, "major"), (9, "minor")), "relative_major_minor")
        self.assertEqual(topology.relation((0, "major"), (3, "minor")), "other_cross_mode")
        self.assertEqual(topology.relation((0, "minor"), (3, "major")), "relative_major_minor")
        self.assertEqual(topology.relation((0, "major"), (0, "minor")), "parallel_mode")

    def test_invalid_keys_rejected(self):
        for value in ((True, "major"), (12, "major"), (-1, "minor"), (1.5, "major"), (0, "dorian"), []):
            with self.subTest(value=value), self.assertRaises(ValueError):
                topology.valid_key(value)

    def test_partition_and_review_denominators(self):
        rows, report = topology.analyze(fixture())
        self.assertEqual(len(rows), 72)
        self.assertEqual(report["external_agreement_50"]["count"], 50)
        disputed = report["external_disagreement_22"]
        self.assertEqual(disputed["pairs"]["openkeyscan_to_essentia"]["families"]["relative_major_minor"], 22)
        self.assertEqual(disputed["review_overlap"]["consensus"]["count"], 0)
        self.assertEqual(report["all_72"]["review_overlap"]["consensus"]["count"], 9)
        self.assertNotIn("track-", json.dumps(report))
        self.assertNotIn("accuracy", json.dumps(report))

    def test_order_independence(self):
        data = fixture()
        expected = topology.analyze(data)
        for report in data.values():
            for rows in report.values():
                rows.reverse()
        self.assertEqual(expected, topology.analyze(data))

    def test_duplicate_and_incomplete_rows_rejected(self):
        for name, field in (("baseline", "tracks"), ("essentia", "rows"), ("openkeyscan", "rows")):
            for mutation in ("duplicate", "missing"):
                data = fixture()
                rows = data[name][field]
                if mutation == "duplicate":
                    rows[1] = copy.deepcopy(rows[0])
                else:
                    rows.pop()
                with self.subTest(name=name, mutation=mutation), self.assertRaises(ValueError):
                    topology.analyze(data)

    def test_mismatched_coverage_and_flags_rejected(self):
        for field, value in (("track", "track-" + "f" * 24), ("agrees_essentia", False),
                             ("canonical_source_unchanged", False)):
            data = fixture()
            data["openkeyscan"]["rows"][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                topology.analyze(data)

    def test_listener_mapping_consensus_and_count_rejected(self):
        for field, value in (("listener2", [9, "minor"]), ("listener2", None), ("openkeyscan", [9, "minor"])):
            data = fixture()
            data["review"]["rows"][0][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                topology.analyze(data)
        data = fixture()
        data["coordinator"]["private_rows"][1]["track"] = data["coordinator"]["private_rows"][0]["track"]
        with self.assertRaisesRegex(ValueError, "duplicate review"):
            topology.analyze(data)

    def test_pinned_hash_rejects_changed_input_before_json_parse(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "input.json"
            path.write_bytes(b"{}")
            with patch.object(topology, "INPUTS", {"fake": ("input.json", hashlib.sha256(b"{}").hexdigest())}):
                self.assertEqual(topology.load_pinned(root), {"fake": {}})
                path.write_bytes(b"not json")
                with self.assertRaisesRegex(ValueError, "hash mismatch"):
                    topology.load_pinned(root)

    def test_existing_or_public_output_refused_before_input_access(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            existing = root / "existing.json"
            existing.write_text("preserve", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "output exists"):
                topology.run(root, existing)
            self.assertEqual(existing.read_text(), "preserve")
            with self.assertRaisesRegex(ValueError, "under build"):
                topology.run(root, root / "public.json")


if __name__ == "__main__":
    unittest.main()
