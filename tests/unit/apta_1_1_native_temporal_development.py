#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import apta_1_1_native_temporal_development as module


class TemporalDevelopmentTests(unittest.TestCase):
    def test_frozen_code_and_stage_b_are_checked_before_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with mock.patch.object(module, "preflight") as corpus:
                with self.assertRaises((ValueError, FileNotFoundError)):
                    module.run(root, root / "missing-report", root / "missing-inventory", root / "output", "b" * 40)
                corpus.assert_not_called()
            self.assertFalse((root / "output").exists())
            with self.assertRaisesRegex(ValueError, "revision"):
                module.source_bindings(root, root, "short")

    def test_hash_drift_and_recheck_are_fail_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "evidence.json"
            path.write_text("{}")
            files = {}
            module.bind(path, module.history.sha256_file(path), files)
            module.recheck(files)
            path.write_text('{"changed":true}')
            with self.assertRaisesRegex(ValueError, "hash mismatch"):
                module.recheck(files)
            with self.assertRaises(ValueError):
                module.bind(path, "bad", {})

    def test_holdout_or_wrong_cohort_header_refused_before_labels(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepared = root / "asap-development-20261022/prepared"
            prepared.mkdir(parents=True)
            (prepared / "manifest.json").write_text(json.dumps({"prepared_splits": ["holdout"]}))
            with mock.patch.object(module.annotated, "load_development") as labels:
                with self.assertRaisesRegex(ValueError, "hash mismatch"):
                    module.preflight("asap", root, {})
                labels.assert_not_called()

    def test_strict_gate_rejects_equality_and_abstention_breaks(self):
        baseline = dict.fromkeys(module.CORRECT, 1)
        candidate = dict.fromkeys(module.CORRECT, 2)
        before, after = dict(baseline), dict.fromkeys(module.CORRECT, 0)
        for value in (before, after):
            value.update(high_confidence_grid_error=0, high_confidence_grid_unknown_context=0)
        delta = module.paired(before, after, {"kind": "accepted"}, {"kind": "ambiguous"})
        self.assertTrue(all(delta[key + "_breaks"] == 1 for key in module.CORRECT))
        self.assertFalse(all(module.gates(baseline, candidate, delta).values()))
        self.assertFalse(module.gates(baseline, baseline, {})["strict_period_improvement"])
        self.assertFalse(module.gates(baseline, baseline, {})["strict_joint_improvement"])
        self.assertTrue(all(module.gates(baseline, candidate, {}).values()))

    def test_phase_only_and_unknown_high_confidence_are_protected(self):
        before = dict.fromkeys(module.CORRECT, 0)
        before.update(confirmed_phase_windows=1, high_confidence_grid_error=0, high_confidence_grid_unknown_context=0)
        after = dict(before, confirmed_phase_windows=0, high_confidence_grid_unknown_context=1)
        changes = module.paired(before, after, {"kind": "accepted"}, {"kind": "no_valid_path"})
        gate = module.gates(before, dict(after, confirmed_period_windows=2, confirmed_joint_windows=2), changes)
        self.assertFalse(gate["zero_confirmed_phase_windows_breaks"])
        self.assertFalse(gate["no_high_confidence_safety_regression"])

    def test_timing_adapter_excludes_truth_meter_and_coarse_proposals(self):
        native = {key: None for key in ("format", "mode", "sample_rate", "channel_count", "input_sha256", "finished",
            "capacity_exhausted", "provisional", "analyzed_first_frame", "source_total_frames", "requested_frames", "analyzed_frames")}
        native.update(private_name="secret", truth={"tempo": 120}, windows=[{"first_frame": 0, "end_frame": 256,
            "dense_trace": {"values": [0.0]}, "meter": {"numerator": 3}, "baseline": {"tempo": 120}, "selected": {"kind": "accepted"}}])
        value = module.timing_input(native)
        self.assertEqual(set(value["windows"][0]), {"first_frame", "end_frame", "dense_trace"})
        self.assertNotIn("truth", value)
        self.assertNotIn("private_name", value)

    def ordered(self, results):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            inventory = root / "inventory.json"
            inventory.write_text("{}")
            called = []
            def preflight(corpus, evidence, bindings):
                called.append(corpus)
                return {"corpus": corpus}
            def run_corpus(context, output, bindings):
                report = {"status": results[len(called) - 1]}
                module.history.write_new(output / "report.json", report)
                return report
            with mock.patch.object(module, "source_bindings", return_value={}), mock.patch.object(module, "preflight", side_effect=preflight), mock.patch.object(module, "run_corpus", side_effect=run_corpus):
                result = module.run(root, root, inventory, root / "output", "b" * 40)
                with self.assertRaisesRegex(ValueError, "overwrite"):
                    module.run(root, root, inventory, root / "output", "b" * 40)
            return called, result

    def test_asap_failure_never_opens_ballroom(self):
        called, result = self.ordered(["rejected"])
        self.assertEqual(called, ["asap"])
        self.assertEqual(result["status"], "rejected")
        self.assertFalse(result["ballroom_run"])

    def test_ballroom_only_after_asap_pass_and_still_diagnostic(self):
        called, result = self.ordered(["passed", "passed"])
        self.assertEqual(called, ["asap", "ballroom"])
        self.assertEqual(result["status"], "passed_both_development_gates")
        self.assertFalse(result["formal_acceptance"])
        self.assertFalse(result["native_selector_implemented"])

    def test_operational_failure_retains_veto_and_stops_order(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with mock.patch.object(module, "source_bindings", return_value={}), mock.patch.object(module, "preflight", side_effect=module.model.ResourceLimit("synthetic cap")) as preflight:
                with self.assertRaises(module.model.ResourceLimit):
                    module.run(root, root, root, root / "output", "b" * 40)
            self.assertEqual(preflight.call_count, 1)
            report = json.loads((root / "output/operational-failure.json").read_text())
            self.assertEqual(report["status"], "operational_veto")
            self.assertFalse(report["retry_or_rescue_allowed"])

    def test_full_240_windows_shared_budgets_frozen_scorer_no_meter_borrowing(self):
        sources, retained = [], []
        totals = module.Counter()
        for number in range(40):
            track = "track-" + f"{number:024x}"
            truth = {"track": track, "meter_numerator": 3 if number < 20 else 4, "meter_denominator": 4,
                     "beat_frames": list(range(24000, 1440001, 24000)), "downbeat_frames": list(range(24000, 1440001, 96000))}
            windows, scores = [], []
            for first in range(0, 1440000, module.history.WINDOW):
                window = {"first_frame": first, "end_frame": min(first + module.history.WINDOW, 1440000),
                          "dense_trace": {"values": [0.0]},
                          "selected": {"kind": "accepted", "anchor_frame": max(24000, (first + 23999) // 24000 * 24000), "tempo_millibpm": 120000, "confidence": 49},
                          "meter": {"kind": "estimated", "numerator": truth["meter_numerator"], "denominator": 4,
                                    "downbeat_frame": 24000, "downbeat_fraction_q32": 0}}
                windows.append(window)
                score = module.annotated.score_window(window, truth, 48000)
                scores.append(score); totals.update(score)
            native = {"format": "test", "mode": "novelty", "sample_rate": 48000, "channel_count": 2,
                      "input_sha256": "a" * 64, "finished": True, "capacity_exhausted": False, "provisional": True,
                      "analyzed_first_frame": 0, "source_total_frames": 1440000, "requested_frames": 1440000,
                      "analyzed_frames": 1440000, "windows": windows}
            sources.append({"truth": truth, "coarse": native, "dense": native})
            retained.append({"track": track, "windows": {"coarse": scores}})
        # Complete-reference denominators: first target is unknown, other five
        # are covered in this synthetic fixture (not the actual ASAP cohort).
        seen = []
        def select(native, index, *, budget):
            self.assertNotIn("meter", native["windows"][index])
            self.assertNotIn("selected", native["windows"][index])
            seen.append(budget)
            first = native["windows"][index]["first_frame"]
            return {"kind": "accepted", "anchor_frame": max(24000, (first + 23999) // 24000 * 24000), "tempo_millibpm": 120000, "confidence": 49}
        context = {"corpus": "asap", "sources": sources, "baseline": {"details": retained, "totals": {"coarse": dict(totals)}},
                   "capture_sha256": "c" * 64, "manifest_sha256": "d" * 64, "labels_sha256": "e" * 64}
        protocol = copy.deepcopy(module.model.PROTOCOL)
        protocol["inputs"]["asap"]["complete_reference_windows"] = totals["reference_complete_windows"]
        with tempfile.TemporaryDirectory() as directory, mock.patch.object(module.model, "select_window", side_effect=select), mock.patch.object(module.model, "PROTOCOL", protocol), mock.patch("builtins.print"):
            result = module.run_corpus(context, Path(directory) / "output", {})
        self.assertEqual(len(seen), 240)
        self.assertEqual(len({id(b) for b in seen}), 40)
        self.assertTrue(all(seen[i] is seen[i // 6 * 6] for i in range(240)))
        self.assertEqual(result["totals"]["temporal"]["meter_emitted"], 0)
        self.assertEqual(result["totals"]["coarse"]["meter_emitted"], 240)
        self.assertEqual(result["totals"]["temporal"]["confirmed_joint_windows"], totals["confirmed_joint_windows"])
        self.assertEqual(result["paired"]["confirmed_joint_windows_breaks"], 0)
        self.assertEqual(result["status"], "rejected")  # Equality cannot pass.


if __name__ == "__main__":
    unittest.main()
