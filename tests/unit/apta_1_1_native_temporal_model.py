#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import copy
import json
import math
import os
from pathlib import Path
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import apta_1_1_native_temporal_model as model


class RecurrenceTests(unittest.TestCase):
    def test_two_best_cells_match_independent_exhaustive_paths(self):
        for periods, values in ((periods,values) for periods in ((2.0,3.0,4.0),(2.25,3.25,4.5))
                               for values in ((0.0,0.125,0.25,0.0,0.5,0.125,0.25),(0.0,)*7)):
            actual = model._build(values, periods, model.SourceBudget())
            all_paths = {}
            for i, value in enumerate(values):
                for j, period in enumerate(periods):
                    paths = [((i + 1) * value, ((i, j),))] if i < period else []
                    for h in range(i):
                        for k in range(len(periods)):
                            if abs(k - j) > 1 or abs((i - h) - period) > 1:
                                continue
                            for score, path in all_paths[h, k]:
                                paths.append((score + ((i-h)*value - abs((i-h)-period) - abs(period-periods[k])), path + ((i,j),)))
                    all_paths[i, j] = paths
                    expected = sorted(paths, key=lambda entry: (-entry[0], entry[1]))[:2]
                    cell = i * actual.stride + 2 * j
                    observed = [(actual.scores[cell+r], actual.path(cell+r+1)) for r in (0,1) if actual.scores[cell+r] != -math.inf]
                    self.assertEqual(observed, expected, (values, i, j))

    def test_lexical_tie_compares_complete_extended_paths(self):
        paths = model._Paths(3, 1, model.SourceBudget())
        paths.previous[2] = 1  # node 3: [(0,0), (1,0)]
        self.assertEqual(paths.path(1), ((0,0),))
        self.assertEqual(paths.path(3), ((0,0), (1,0)))
        self.assertTrue(model._Paths.earlier_extension(paths, 3, 1, 2, 0))
        self.assertFalse(model._Paths.earlier_extension(paths, 1, 3, 2, 0))

    def test_recurrence_forbids_abrupt_tempo_state_jump(self):
        paths = model._build((0.0, 0.125, 0.0, 0.25, 0.0, 0.5, 0.125, 0.0), (2.0,3.0,4.0), model.SourceBudget())
        for node in range(1, len(paths.scores)+1):
            if paths.scores[node-1] == -math.inf: continue
            path = paths.path(node)
            for (h,k), (i,j) in zip(path, path[1:]):
                self.assertLessEqual(abs(j-k), 1)
                self.assertLessEqual(abs(i-h-(2.0,3.0,4.0)[j]), 1)


class LatticeTests(unittest.TestCase):
    def path(self): return tuple((16 + 272*n, 2) for n in range(12))

    def test_fit_uses_source_offsets_and_exact_q32_output(self):
        fit = model._fit(self.path(), 0, 2*model.WINDOW, 3*model.WINDOW)
        self.assertEqual(fit, (2192*256, 41360))
        offset = 1 << 55
        self.assertEqual(model._fit(self.path(), offset, offset+2*model.WINDOW, offset+3*model.WINDOW),
                         (offset+fit[0], fit[1]))
        period_q32 = (48000*60000 << 32) // fit[1]
        self.assertLess(abs(period_q32/(1<<32) - 272*256), 1)
        # Independently specified true pulse positions, including full reference context.
        anchor = fit[0] << 32
        while anchor < 3*model.WINDOW << 32:
            frame = anchor/(1<<32)
            nearest = min((16*256+n*272*256 for n in range(14)), key=lambda x: abs(x-frame))
            self.assertLess(abs(frame-nearest), 0.10*272*256)
            anchor += period_q32

    def test_short_path_and_abrupt_fit_rejected_slow_drift_retained(self):
        self.assertIsNone(model._fit(self.path()[:7], 0, 0, model.WINDOW))
        abrupt = tuple((i,0) for i in (16,112,208,304,512,720,928,1136))
        self.assertIsNone(model._fit(abrupt,0,0,1137*256))
        gradual = tuple((i,0) for i in (16,110,205,301,398,496,595,695))
        self.assertIsNotNone(model._fit(gradual,0,0,696*256))

    def test_distinct_phases_and_tempo_aliases_use_converted_q32(self):
        self.assertFalse(model._distinct(2400,120000,0,120000))
        self.assertTrue(model._distinct(2401,120000,0,120000))
        self.assertTrue(model._distinct(12000,120000,0,120000))
        self.assertFalse(model._distinct(24000,120000,0,120000))
        self.assertTrue(model._distinct(0,60000,0,120000))
        self.assertTrue(model._distinct(0,240000,0,120000))

    def test_same_period_phase_competitor_is_not_discarded(self):
        budget = model.SourceBudget()
        paths = model._Paths(3,1,budget)
        terminals = model._Terminals()
        terminals.append(1.0,1,0,120000)
        terminals.append(1.0,3,12000,120000)
        result = model._rank(terminals,paths,budget)
        self.assertEqual(result["kind"], "ambiguous")
        self.assertEqual(result["margin"], 0.0)
        terminals.scores[1] = 0.5
        result = model._rank(terminals,paths,budget)
        self.assertEqual(result["kind"], "accepted")
        self.assertEqual(result["anchor_frame"], 0)
        self.assertEqual(result["confidence"], 49)
        terminals.scores[0] = terminals.scores[1] = 0.0
        self.assertEqual(model._rank(terminals,paths,budget)["kind"], "insufficient_support")


class BoundaryTests(unittest.TestCase):
    def test_bad_observations_and_source_geometry_rejected_before_graph(self):
        with mock.patch.object(model,"_build") as graph:
            for value in (float("nan"),float("inf"),-0.01,1.1,0.1,True,"0"):
                with self.assertRaises(ValueError): model.select([value],0,0,1)
            for args in (([0.0],1,0,1),([0.0],0,0,257),([0.0]*3073,0,0,3073*256),([0.0],0,0,1<<64)):
                with self.assertRaises(ValueError): model.select(*args)
            with self.assertRaises(ValueError): model.select([0.125],0,0,1)
            graph.assert_not_called()

    def native(self, count=4):
        windows=[]
        for index in range(count):
            windows.append({"first_frame":index*model.WINDOW,"end_frame":(index+1)*model.WINDOW,
                "dense_trace":{"format":"libapta-dense-novelty-1","complete":True,"count":1024,
                "first_bin":1024*index,"bin_frames":256,"last_bin_frames":256,
                "startup_bins":15 if index==0 else 0,"values":[0.0]*1024}})
        return {"format":"libapta-history-diagnostic-1","mode":"novelty","sample_rate":48000,
                "channel_count":2,"input_sha256":"a"*64,"windows":windows,
                "finished":True,"capacity_exhausted":False,"provisional":True,"analyzed_first_frame":0,
                "source_total_frames":min(count*model.WINDOW,1440000),"requested_frames":1440000,
                "analyzed_frames":min(count*model.WINDOW,1440000)}

    def test_context_is_causal_and_source_local(self):
        native=self.native(6)
        native["windows"][0]["dense_trace"]["values"]=[float("nan")]
        native["windows"][5]["dense_trace"]["values"]=[float("nan")]
        with mock.patch.object(model,"select",return_value={"marker":True}) as select:
            self.assertEqual(model.select_window(native,4),{"marker":True})
            args=select.call_args.args
            self.assertEqual(len(args[0]),3072)
            self.assertEqual(args[1:],(2*model.WINDOW,4*model.WINDOW,5*model.WINDOW))
            select.reset_mock()
            self.assertEqual(model.select_window(self.native(1),0),{"marker":True})
            self.assertEqual(select.call_args.args[1:],(0,0,model.WINDOW))

    def test_missing_partial_or_incomplete_context_rejected(self):
        for mutate in (lambda n: n["windows"][1]["dense_trace"].update(complete=False),
                       lambda n: n["windows"][1]["dense_trace"].update(count=100),
                       lambda n: n["windows"][1].update(end_frame=2*model.WINDOW-17)):
            native=self.native()
            mutate(native)
            with self.assertRaises(ValueError): model.select_window(native,2)
        for field,bad in (("finished",False),("capacity_exhausted",True),("provisional",False),
                          ("analyzed_frames",1),("requested_frames",1440001),("input_sha256","holdout")):
            native=self.native()
            native[field]=bad
            with self.assertRaises(ValueError): model.select_window(native,2)

    def test_resource_limits_are_vetoes_and_shared_source_budget_does_not_reset(self):
        with mock.patch.object(model,"_rss_kib",return_value=1):
            budget=model.SourceBudget()
            with self.assertRaises(model.ResourceLimit): budget.check(model.LIMITS["host_numeric_workspace_bytes_max"]+1)
        with mock.patch.object(model,"_rss_kib",return_value=model.LIMITS["host_rss_kib_max"]+1):
            with self.assertRaises(model.ResourceLimit): model.SourceBudget().check()
        with mock.patch.object(model,"_rss_kib",return_value=1), mock.patch.object(model.time,"monotonic",side_effect=[0.0,60.0,121.0]):
            budget=model.SourceBudget(); budget.check()
            with self.assertRaises(model.ResourceLimit): budget.check()

    def test_silence_and_short_input_never_emit(self):
        result=model.select([0.0]*512,0,0,512*256)
        self.assertNotEqual(result["kind"],"accepted")
        self.assertNotEqual(model.select([0.0]*17,0,0,17*256)["kind"],"accepted")


class FullSyntheticSource(unittest.TestCase):
    def test_weak_pulses_missing_beat_and_maximum_context_source(self):
        # These coordinates are specified before running the model: 272-bin
        # pulses cannot have a half-tempo alias inside the registered range.
        values=[0.0]*5625
        for index in range(16,len(values),272): values[index]=0.125
        values[16+4*272]=0.0  # retain the missing beat's ordinal
        budget=model.SourceBudget()
        outcomes=[]
        for index in range(6):
            first=max(0,index-2)*1024
            end=min(len(values),(index+1)*1024)
            result=model.select(values[first:end],first*256,index*model.WINDOW,end*256,budget=budget)
            outcomes.append(result)
        self.assertEqual(outcomes[2]["kind"],"accepted")
        self.assertEqual(outcomes[2]["anchor_frame"],2192*256)
        self.assertEqual(outcomes[2]["tempo_millibpm"],41360)
        self.assertEqual([i for i,_ in outcomes[2]["path"]], list(range(16,3072,272)))
        self.assertLessEqual(budget.numeric_peak,model.LIMITS["host_numeric_workspace_bytes_max"])
        artifact=os.environ.get("APTA_TEMPORAL_SYNTHETIC_REPORT")
        if artifact:
            import hashlib
            value={"format":"libapta-temporal-synthetic-source-1","synthetic_only":True,
                "protocol_sha256":model.PROTOCOL_SHA256,"fixture":"5625 bins, weak 272-bin pulse, one missing beat",
                "results":outcomes,"numeric_peak_bytes":budget.numeric_peak,"rss_peak_kib":budget.rss_peak,
                "model_sha256":hashlib.sha256(Path(model.__file__).read_bytes()).hexdigest(),
                "test_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
            with Path(artifact).open("x",encoding="utf-8") as stream:
                stream.write(json.dumps(value,indent=2,sort_keys=True)+"\n")


if __name__=="__main__": unittest.main()
