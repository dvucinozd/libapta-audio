#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import ast
from collections import Counter
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/"tools"))
import apta_1_1_dense_decoder_diagnosis as audit


class DiagnosisTests(unittest.TestCase):
    def truth(self):
        return {"beat_frames": list(range(0, 240001, 24000))}

    def test_constant_exact_intervals_and_unknown_last(self):
        frames, rows = list(range(0, 240000, 24000)), list(range(0, 250, 25))
        counts, errors, offsets, changes = audit.interval_audit(frames, rows, self.truth())
        self.assertEqual(counts["row_pass_actual_pass"], 9)
        self.assertEqual(counts["unknown_successor_points"], 1)
        self.assertEqual(errors, [0.]*9)
        self.assertEqual(offsets, [0]*10)
        self.assertEqual(changes, [0]*9)

    def test_independent_refinement_can_break_exact_row_intervals(self):
        rows = list(range(25, 225, 25))
        frames = [r*960+(-160 if i%2 == 0 else 160) for i,r in enumerate(rows)]
        counts, errors, _offsets, changes = audit.interval_audit(frames, rows, self.truth())
        self.assertEqual(counts["row_pass_actual_fail"], 7)
        self.assertEqual(counts["refinement_difference_exceeds_one_percent"], 7)
        self.assertEqual(changes, [320]*7)
        self.assertAlmostEqual(errors[0], 100*320/24000)

    def test_refinement_can_repair_lattice_error_arithmetically(self):
        truth = {"beat_frames": list(range(0, 250001, 25000))}
        counts, *_ = audit.interval_audit([460, 25460], [0, 27], truth)
        self.assertEqual(counts["row_fail_actual_pass"], 1)

    def test_lattice_coverage_has_no_legal_one_percent_multiple(self):
        truth = {"beat_frames": list(range(0, 302401, 30240))}
        counts, *_ = audit.interval_audit([30240, 60480], [31, 63], truth)
        self.assertEqual(counts["local_target_without_one_percent_lattice_multiple"], 1)

    def test_outside_reference_and_unknown_are_distinct(self):
        truth = {"beat_frames": [24000, 48000, 72000]}
        counts, *_ = audit.interval_audit([0, 24000, 48000, 72000], [0,25,50,75], truth)
        self.assertEqual(counts["outside_reference_points"], 1)
        self.assertEqual(counts["scored_points"], 3)
        self.assertEqual(counts["known_intervals"], 2)
        self.assertEqual(counts["unknown_successor_points"], 1)

    def test_invalid_rows_or_refinement_reject(self):
        for frames, rows in (([0,24000],[0]), ([0,24000],[0,0]), ([0,0],[0,25]), ([481,24000],[0,25])):
            with self.assertRaises(ValueError):
                audit.interval_audit(frames,rows,self.truth())

    def test_reason_decomposition_matches_original_scorer(self):
        truth = self.truth()
        cases = [([(0,24000)],"pass"), ([],"empty"), ([(0,None)],"unknown_successor"),
                 ([(0,25000)],"known_period_error"), ([(10000,24000)],"phase_error"),
                 ([(10000,25000)],"phase_error+known_period_error")]
        for points, expected in cases:
            scaled = [(p*audit.Q,d*audit.Q if d is not None else None) for p,d in points]
            score, reason = audit.window_reasons(scaled,truth,0,24000)
            self.assertEqual(reason,expected)
            self.assertEqual(bool(score["joint_windows"]),expected=="pass")

    def test_incomplete_context_is_not_a_period_error(self):
        score, reason = audit.window_reasons([(0,24000*audit.Q)], self.truth(),0,300000)
        self.assertEqual(reason,"incomplete_reference")
        self.assertEqual(score["joint_windows"],0)

    def test_annotation_median_target_can_disagree_with_own_adjacent_gap(self):
        truth = {"beat_frames": [0,24000,48000,75000,99000,123000,147000]}
        counts = audit.annotation_consistency(truth,147001)
        self.assertEqual(counts["in_source_reference_pairs"],6)
        self.assertEqual(counts["outside_one_percent_local_median"],1)

    def test_nearest_rank_distribution_and_empty(self):
        self.assertEqual(audit.distribution([])["p50"],None)
        values = audit.distribution(list(range(1,101)))
        self.assertEqual(values,{"count":100,"p50":50,"p90":90,"p99":99,"max":100})

    def test_tool_has_no_decoder_or_inference_import(self):
        tree = ast.parse(Path(audit.__file__).read_text())
        imports = [alias.name for node in ast.walk(tree) if isinstance(node,ast.Import) for alias in node.names]
        self.assertFalse(any("apta_dense_decoder" in n or n in ("torch","numpy") for n in imports))

    def test_generated_full_audit_binds_replays_and_keeps_details_private(self):
        with tempfile.TemporaryDirectory(prefix="libapta-diagnosis-") as temp:
            root = Path(temp)
            candidates, evaluation, prepared, output = [root/n for n in ("candidates","evaluation","prepared","audit")]
            write = audit.io.write_new
            frames, rows = [0,24000,48000,72000], [0,25,50,75]
            windows = [{"first_frame":0,"end_frame":96000,"beat_ordinals":[0,1,2,3]}]
            points = [(p*audit.Q,24000*audit.Q if i<3 else None) for i,p in enumerate(frames)]
            score = audit.representation.score_points(points,self.truth(),0,96000)
            totals = Counter()
            truths, entries, details = [], [], []
            for i in range(100):
                track = "generated-"+str(i)
                truths.append(dict(self.truth(),track=track))
                path = candidates/"candidates"/(track+".json")
                write(path,{"events":{"beat_frames":frames,"beat_rows":rows},"windows":windows,"total_frames":96000})
                entries.append({"track":track,"sha256":audit.io.sha(path)})
                path = evaluation/(track+".json")
                write(path,{"scores":{"candidate_actual":[score],"ols_actual":[score],"candidate_nominal":[{"joint_windows":1}]}})
                details.append({"track":track,"sha256":audit.io.sha(path)})
                totals.update(score)
            manifest = prepared/"manifest.private.json"
            write(manifest,{"holdout_performances_opened":False,"tracks":truths})
            report = {"source_revision":"generated", "protocol_sha256":"generated", "status":"REJECTED", "integrity_passed":True,
                "details":details,"totals":{"candidate_actual":totals,"ols_actual":totals},
                "paired":{"actual":{"joint_breaks":0}},"bindings":{}}
            for name in ("apta_1_1_event_representation.py","apta_1_1_asap_meter_validation.py","apta_1_1_neural_reference.py","apta_1_1_learned_piecewise.py"):
                path = audit.ROOT/"tools"/name
                report["bindings"][str(path.resolve())] = audit.io.sha(path)
            identity = {}
            for phase,directory,name,value in (("construct",candidates,"candidates-frozen.private.json",{"source_revision":"generated","protocol_sha256":"generated","candidates":entries}),
                ("verify",Path(str(candidates)+"-verification"),"report.private.json",{"source_revision":"generated","protocol_sha256":"generated","sources_verified":100}),
                ("evaluate",evaluation,"report.private.json",report)):
                path = directory/name
                write(path,value)
                parent = Path(str(directory)+"-supervision")/"report.json"
                write(parent,{"status":"PASS","completion_sha256":audit.io.sha(path)})
                identity[phase] = {"completion_sha256":audit.io.sha(path),"supervisor_report_sha256":audit.io.sha(parent)}
            public = root/"public.json"
            write(public,{"status":"REJECTED","integrity_passed":True,"source_revision":"generated","protocol_sha256":"generated", "phase_identity":identity,
                "input_identity":{"prepared_manifest_sha256":audit.io.sha(manifest)}})
            with mock.patch.object(audit,"RESULT",public), mock.patch.object(audit,"RESULT_SHA256",audit.io.sha(public)), mock.patch.object(audit.io,"source_snapshot",return_value="generated-audit"), mock.patch.object(audit.io,"check_source"):
                observed = []
                result = audit.diagnose(candidates,evaluation,prepared,output,
                    lambda track,candidate,saved,truth: observed.append(track))
                self.assertEqual(len(observed),100)
                self.assertEqual(result["interval_counts"]["actual_period_correct"],300)
                self.assertEqual(result["actual_window_reasons"],{"unknown_successor":100})
                self.assertNotIn("track",json.dumps(result))
                self.assertTrue((output/"details.private.json").exists())
                with self.assertRaises(ValueError):
                    audit.diagnose(candidates,evaluation,prepared,output)
                # A changed source score is rejected before any replacement report.
                (evaluation/"generated-0.json").write_text("{}")
                with self.assertRaises(ValueError):
                    audit.diagnose(candidates,evaluation,prepared,root/"corrupt-audit")


if __name__ == "__main__":
    unittest.main()
