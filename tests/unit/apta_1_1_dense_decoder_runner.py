#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""DD-03 generated-only binding, isolation, score and watchdog checks."""
from collections import Counter
from contextlib import ExitStack
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/"tools"))
import apta_1_1_dense_decoder as runner
import apta_dense_runner_support as io
import apta_neural_dense as codec
import apta_dense_decoder as model


def metric_stub(truth, events):
    # Unit-only metric stand-in. Real mir_eval helper has a separate runtime test.
    return {k:float(events["beat_frames"] == truth["beat_frames"][:-1]) for k in runner.METRICS}


class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="libapta-dense-runner-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.config = deepcopy(runner.protocol())

    def fixture(self, count=2, event_metric=metric_stub):
        import apta_1_1_neural_reference as ref
        import apta_1_1_neural_comparison as comparison
        import apta_1_1_event_representation as representation
        import apta_1_1_learned_piecewise as previous
        dense, legacy, prepared = [self.root/k for k in ("dense","legacy","prepared")]
        for path in (dense,legacy/"beat_this",legacy/"scoring",prepared):
            path.mkdir(parents=True)
        sources, details, oldoutputs, truths, scored, counts = [], [], [], [], [], Counter()
        totals = {"coarse":Counter(),"beat_this":Counter()}
        diagnostic = {"nominal":Counter(),"actual":Counter()}
        raw = [-8.] * 601
        down = [-8.] * 601
        for i,t in enumerate(range(0,600,25)):
            raw[t]=8.
            if i%4==0:
                down[t]=8.
        payload=codec.encode(raw,down)
        total=600*960
        windows=[{"first_frame":0,"end_frame":300*960},
                 {"first_frame":300*960,"end_frame":total}]
        events={"beat_frames":list(range(0,total,24000)),"downbeat_frames":list(range(0,total,96000)),
                "beat_seconds":[i/2 for i in range(24)],"downbeat_seconds":[i*2 for i in range(6)],
                "confidence":None,"input_sample_rate":22050,
                "exclusive_endpoint_events":{"beat_seconds":[],"downbeat_seconds":[]}}
        for i in range(count):
            track="generated-"+str(i)
            truth={"track":track,"requested_frames":total,"beat_frames":list(range(0,total+1,24000)),
                   "downbeat_frames":list(range(0,total+1,96000)),"meter_numerator":4,"meter_denominator":4,
                   "genre":"generated"}
            truths.append(truth)
            controls=[ref.project(events["beat_frames"],events["downbeat_frames"],w["first_frame"],w["end_frame"])
                      for w in windows]
            oldscores=[ref.annotated.score_window(w,truth,48000) for w in controls]
            coarse={"windows":controls,"requested_frames":total,"sample_rate":48000}
            metrics=event_metric(truth,events)
            saved={"track":track,"coarse":oldscores,
                   "models":{"beat_this":{"event_metrics":metrics,
                              "windows":[{"candidate":w,"score":s} for w,s in zip(controls,oldscores)]}}}
            io.write_new(legacy/"scoring"/(track+"-coarse.json"),coarse)
            io.write_new(legacy/"scoring"/(track+".json"),saved)
            scored.append({"track":track,"sha256":io.sha(legacy/"scoring"/(track+".json"))})
            for value in oldscores:
                totals["coarse"].update(value)
                totals["beat_this"].update(value)
            op=previous.control_points(controls,events["beat_frames"])
            for mode in ("nominal","actual"):
                for value in previous.score_point_windows(op,truth,windows,mode=="actual"):
                    diagnostic[mode].update(value)
            audio_hash=hashlib.sha256(track.encode()).hexdigest()
            old={"track":track,"audio_sha256":audio_hash,"events":deepcopy(events)}
            io.write_new(legacy/"beat_this"/(track+".json"),old)
            oldoutputs.append({"track":track,"events_sha256":io.sha(legacy/"beat_this"/(track+".json"))})
            (dense/(track+".f32")).write_bytes(payload)
            geometry=codec.mapping(total,total*22050//48000,601,windows)
            agg=codec.coverage(601,[-6],[613])
            agg.update(starts=[-6],chunk_lengths=[613])
            record={"track":track,"audio_sha256":audio_hash,"payload_sha256":io.sha(dense/(track+".f32")),
                    "payload_bytes":len(payload),"events":deepcopy(events),"geometry":geometry,"aggregation":agg,
                    "statistics":codec.statistics(raw,down),"bit_exact_logit_roundtrip":True,
                    "exact_legacy_event_parity":True}
            io.write_new(dense/(track+".json"),record)
            details.append({"track":track,"payload_sha256":record["payload_sha256"],
                            "record_sha256":io.sha(dense/(track+".json"))})
            counts.update(sources=1,windows=2,rows=601,payload_bytes=len(payload),source_owned_rows=600,
                          exclusive_endpoint_rows=1,beyond_source_rows=0,legacy_beat_events=24,
                          legacy_downbeat_events=6,exclusive_endpoint_beat_events=0,exclusive_endpoint_downbeat_events=0)
        capture={"source_revision":self.config["inputs"]["dense_source_revision"],
                 "protocol_sha256":self.config["inputs"]["dense_protocol_sha256"],
                 "complete":True,"read_labels":False,"musical_scoring":False,"new_decoder":False,
                 "details":details,"counts":dict(counts),
                 "bindings":{str((legacy/"scoring"/(r["track"]+"-coarse.json")).resolve()):
                             io.sha(legacy/"scoring"/(r["track"]+"-coarse.json")) for r in details}}
        for key in ("all_100_original_saved_and_decoded_events_identical","all_f32_payloads_bit_exact",
                    "all_rows_aggregation_assigned","all_source_window_boundary_rows_retained"):
            capture[key]=True
        io.write_new(dense/"report.private.json",capture)
        io.write_new(legacy/"beat_this/report.json",{"outputs":oldoutputs})
        io.write_new(prepared/"manifest.private.json",{"holdout_performances_opened":False,"tracks":truths})
        means=event_metric(truths[0],events)
        oldreport={"details":scored,"totals":totals,"event_means":{"beat_this":means},
                   "by_genre":{"generated":{"beat_this":{"tracks":count,"event_means":means}}}}
        io.write_new(legacy/"scoring/report.json",oldreport)
        for k,path in (("dense_report_sha256",dense/"report.private.json"),
                       ("legacy_worker_report_sha256",legacy/"beat_this/report.json"),
                       ("prepared_manifest_sha256",prepared/"manifest.private.json"),
                       ("legacy_scoring_report_sha256",legacy/"scoring/report.json")):
            self.config["inputs"][k]=io.sha(path)
        self.config["inputs"].update(sources=count,windows=count*2,reference_complete_windows=count*2,
                                     reference_incomplete_windows=0)
        self.config["baseline"].update(ols_nominal_joint_windows=diagnostic["nominal"]["joint_windows"],
            ols_nominal_complete_sequence_windows=diagnostic["nominal"]["complete_sequence_windows"],
            ols_actual_joint_windows=diagnostic["actual"]["joint_windows"],
            ols_actual_complete_sequence_windows=diagnostic["actual"]["complete_sequence_windows"],
            coarse_joint_windows=totals["coarse"]["confirmed_joint_windows"],
            original_coarse_to_ols_joint_breaks=0)
        return dense,legacy,prepared

    def patched(self):
        stack=ExitStack()
        stack.enter_context(mock.patch.object(runner,"protocol",return_value=self.config))
        stack.enter_context(mock.patch.object(io,"source_snapshot",return_value="generated-revision"))
        stack.enter_context(mock.patch.object(io,"check_source"))
        return stack

    def construct(self,dense,legacy,output=None):
        output=output or self.root/"candidates-attempt"
        with self.patched():
            result=runner.construct(dense,legacy,output)
        # Unit-only completion envelope. Actual parent process enforcement is
        # exercised separately; full registered CLI preflight remains DD-04.
        io.write_new(Path(str(output)+"-supervision")/"report.json",
                     {"status":"PASS","protocol_sha256":model.PROTOCOL_SHA256,
                      "completion_sha256":io.sha(output/"candidates-frozen.private.json")})
        import apta_dense_decoder_verify as verifier
        verification = Path(str(output)+"-verification")
        with self.patched():
            verifier.cohort(output, legacy, verification)
        io.write_new(Path(str(verification)+"-supervision")/"report.json",
                     {"status":"PASS","protocol_sha256":model.PROTOCOL_SHA256,
                      "completion_sha256":io.sha(verification/"report.private.json")})
        return output,result

    def test_protocol_matches_decoder_and_no_preflight_is_not_authorization(self):
        self.assertEqual(runner.protocol()["format"],"libapta-dense-decoder-1")
        with mock.patch.object(runner,"PREFLIGHT",self.root/"missing.json"), self.assertRaises(ValueError):
            runner.require_preflight()

    def test_partial_preflight_or_wrong_component_does_not_enable_cli(self):
        path=self.root/"preflight.json"
        io.write_new(path,{"protocol_sha256":model.PROTOCOL_SHA256,"generated_preflight_passed":True})
        with mock.patch.object(runner,"PREFLIGHT",path),self.assertRaises(ValueError):
            runner.require_preflight()
        value={"protocol_sha256":model.PROTOCOL_SHA256,"component_sha256":{}}
        value.update({key:True for key in ("generated_preflight_passed","independent_verifier_passed",
                                          "runner_isolation_passed","resource_enforcement_passed")})
        path.write_text(json.dumps(value))
        with mock.patch.object(runner,"PREFLIGHT",path),self.assertRaises(ValueError):
            runner.require_preflight()

    def test_construct_parser_has_no_labels_or_prepared_option(self):
        for option in ("--prepared","--labels"):
            with mock.patch("sys.stderr"),self.assertRaises(SystemExit):
                runner.parser().parse_args(["construct","--dense","a","--legacy","b","--output","c",option,"d"])

    def test_fresh_import_does_not_import_evaluators_or_numpy_or_torch(self):
        code=("import sys; sys.path.insert(0,"+repr(str(ROOT/"tools"))+");"
              "import apta_1_1_dense_decoder;"
              "assert not any(x in sys.modules for x in "
              "['apta_1_1_neural_comparison','apta_1_1_event_representation',"
              "'apta_1_1_learned_piecewise','numpy','torch'])")
        subprocess.run([sys.executable,"-c",code],check=True,timeout=10)

    def test_exclusive_canonical_writer_matches_existing_helper(self):
        import apta_1_1_native_history_development as history
        a,b=self.root/"a.json",self.root/"b.json"
        value={"z":[1,0.5],"a":{"unicode":"ž","empty":[]}}
        io.write_new(a,value)
        history.write_new(b,value)
        self.assertEqual(a.read_bytes(),b.read_bytes())
        self.assertEqual(io.sha(a),history.sha256_file(a))
        with self.assertRaises(FileExistsError):
            io.write_new(a,value)

    def test_io_binding_detects_mutation_and_symlinks(self):
        p=self.root/"a"
        p.write_bytes(b"original")
        bindings={}
        io.bind(p,io.sha(p),bindings)
        p.write_bytes(b"changed")
        with self.assertRaises(ValueError):
            io.recheck(bindings)
        q=self.root/"link"
        q.symlink_to(p)
        with self.assertRaises(ValueError):
            io.bind(q,io.sha(p),{})

    def test_source_snapshot_refuses_dirty_checkout(self):
        with mock.patch.object(io,"git",side_effect=["revision","revision"," M changed"]):
            with self.assertRaises(ValueError):
                io.source_snapshot(self.root,{})

    def test_runtime_recheck_rejects_unbound_late_import(self):
        path=self.root/"new-runtime.py"
        path.write_text("late dependency")
        def late(bindings,expected=None):
            bindings[str(path)]=io.sha(path)
            return 1
        with mock.patch.object(io,"runtime_snapshot",side_effect=late),self.assertRaises(ValueError):
            io.runtime_recheck({})

    def test_private_output_rejects_repo_existing_and_input_overlap(self):
        with self.assertRaises(ValueError):
            io.external(ROOT/"output",ROOT)
        for output,inputs in ((self.root,(self.root/"input",)),
                              (self.root/"input"/"child",(self.root/"input",))):
            with self.assertRaises(ValueError):
                io.disjoint_output(output,inputs)

    def test_duplicate_and_unsafe_cohort_ids_fail(self):
        for rows in ([{"track":"same"},{"track":"same"}],[{"track":"../escape"}],[{"track":"ok"}]):
            with self.assertRaises(ValueError):
                runner.ids_in_order(rows,2)

    def test_constructs_and_freezes_all_generated_sources_without_reading_labels(self):
        dense,legacy,prepared=self.fixture()
        forbidden={prepared/"manifest.private.json",legacy/"scoring/report.json"}
        original=Path.read_text
        def guarded(path,*args,**kwargs):
            if path in forbidden:
                raise AssertionError("construction must not parse labels or old scores")
            return original(path,*args,**kwargs)
        with mock.patch.object(Path,"read_text",guarded):
            output,complete=self.construct(dense,legacy)
        self.assertTrue(complete["all_candidates_complete"])
        self.assertFalse(complete["reference_labels_loaded"])
        self.assertEqual(len(complete["candidates"]),2)
        for row in complete["candidates"]:
            path=output/"candidates"/(row["track"]+".json")
            self.assertEqual(io.sha(path),row["sha256"])
            value=json.loads(path.read_text())
            self.assertEqual(value["status"],"candidate_events")
            job=json.loads((output/"jobs"/(row["track"]+".job.json")).read_text())
            self.assertEqual(set(job),{"payload","payload_sha256","total_frames","windows","result"})
        with self.patched(),self.assertRaises(ValueError):
            runner.construct(dense,legacy,output)

    def test_changed_or_absent_labels_cannot_change_frozen_candidate_bytes(self):
        dense,legacy,prepared=self.fixture(count=1)
        first,_=self.construct(dense,legacy,self.root/"first")
        labels=prepared/"manifest.private.json"
        labels.write_text('{"unrelated":"changed labels"}')
        second,_=self.construct(dense,legacy,self.root/"second")
        labels.unlink()
        third,_=self.construct(dense,legacy,self.root/"third")
        paths=[p/"candidates/generated-0.json" for p in (first,second,third)]
        self.assertEqual([io.sha(p) for p in paths],[io.sha(paths[0])]*3)

    def test_payload_corruption_stops_before_any_candidate(self):
        dense,legacy,_=self.fixture(count=1)
        payload=dense/"generated-0.f32"
        data=bytearray(payload.read_bytes())
        data[-1]^=1
        payload.write_bytes(data)
        output=self.root/"invalid"
        with self.patched(),self.assertRaises(ValueError):
            runner.construct(dense,legacy,output)
        self.assertFalse((output/"candidates-frozen.private.json").exists())

    def test_mid_cohort_worker_failure_preserves_partial_outputs_without_complete_freeze(self):
        dense,legacy,_=self.fixture()
        output=self.root/"partial"
        original=io.supervise
        calls=[]
        def fail_second(*args,**kwargs):
            calls.append(1)
            if len(calls)==2:
                raise ValueError("generated worker failure")
            return original(*args,**kwargs)
        with self.patched(),mock.patch.object(io,"supervise",side_effect=fail_second),self.assertRaises(ValueError):
            runner.construct(dense,legacy,output)
        self.assertTrue((output/"candidates/generated-0.json").is_file())
        self.assertFalse((output/"candidates-frozen.private.json").exists())

    def test_duplicate_capture_cohort_rejected_before_workers(self):
        dense,legacy,_=self.fixture()
        p=dense/"report.private.json"
        value=json.loads(p.read_text())
        value["details"][1]=value["details"][0]
        p.write_text(json.dumps(value))
        self.config["inputs"]["dense_report_sha256"]=io.sha(p)
        with self.patched(),self.assertRaises(ValueError):
            runner.construct(dense,legacy,self.root/"bad")

    def test_incomplete_freeze_rejected_before_label_access(self):
        dense,legacy,_=self.fixture(count=1)
        output,complete=self.construct(dense,legacy)
        p=output/"candidates-frozen.private.json"
        complete["all_candidates_complete"]=False
        p.write_text(json.dumps(complete))
        with self.patched(),self.assertRaises(ValueError):
            runner.validate_frozen(output,legacy,self.config,{})

    def test_candidate_mutation_and_watchdog_envelope_mismatch_rejected(self):
        dense,legacy,_=self.fixture(count=1)
        output,_=self.construct(dense,legacy)
        p=output/"candidates/generated-0.json"
        original=p.read_bytes()
        p.write_text("{}")
        with self.patched(),self.assertRaises(ValueError):
            runner.validate_frozen(output,legacy,self.config,{})
        p.write_bytes(original)
        envelope=Path(str(output)+"-supervision")/"report.json"
        envelope.write_text('{"status":"PASS","completion_sha256":"wrong"}')
        with self.patched(),self.assertRaises(ValueError):
            runner.validate_frozen(output,legacy,self.config,{})

    def test_actual_intervals_cross_windows_and_final_successor_stays_unknown(self):
        candidate={"events":{"beat_frames":[0,24000,48000]},
                   "windows":[{"first_frame":0,"end_frame":48000},{"first_frame":48000,"end_frame":72000}]}
        points=runner.actual_points(candidate)
        self.assertEqual(points[0],[(0,24000*runner.Q),(24000*runner.Q,24000*runner.Q)])
        self.assertEqual(points[1],[(48000*runner.Q,None)])

    def test_changed_saved_baseline_tuple_cannot_be_scored_as_valid_control(self):
        dense,legacy,prepared=self.fixture(count=1)
        oldevents=json.loads((legacy/"beat_this/generated-0.json").read_text())["events"]
        candidate=model.construct(*codec.decode((dense/"generated-0.f32").read_bytes()),600*960,
                                  [{"first_frame":0,"end_frame":300*960},
                                   {"first_frame":300*960,"end_frame":600*960}])
        saved=json.loads((legacy/"scoring/generated-0.json").read_text())
        saved["models"]["beat_this"]["windows"][0]["candidate"]["selected"]["anchor_frame"]+=1
        truth=json.loads((prepared/"manifest.private.json").read_text())["tracks"][0]
        coarse=json.loads((legacy/"scoring/generated-0-coarse.json").read_text())
        geometry=[{k:w[k] for k in ("first_frame","end_frame")} for w in coarse["windows"]]
        with self.assertRaises(ValueError):
            runner.score_source(candidate,oldevents,geometry,truth,saved,coarse,metric_stub)

    def passing_gates(self):
        totals={"candidate_nominal":{"joint_windows":188,"complete_sequence_windows":179},
                "candidate_actual":{"complete_sequence_windows":98}}
        paired={"ols":{k+"_breaks":0 for k in ("period","phase","joint","meter","downbeat")},
                "coarse":{"joint_breaks":0},"actual":{k+"_breaks":0 for k in ("period","phase","joint")}}
        means={k:.8 for k in runner.METRICS}
        return totals,paired,means

    def test_all_gates_are_conjunctive_and_each_failure_is_retained(self):
        totals,paired,means=self.passing_gates()
        self.assertTrue(all(runner.gates(self.config,totals,paired,means,means).values()))
        for group in paired:
            for key in paired[group]:
                altered=deepcopy(paired)
                altered[group][key]=1
                self.assertFalse(all(runner.gates(self.config,totals,altered,means,means).values()))
        for group in totals:
            for key in totals[group]:
                altered=deepcopy(totals)
                altered[group][key]-=1
                self.assertFalse(all(runner.gates(self.config,altered,paired,means,means).values()))
        for metric in runner.METRICS:
            worse=dict(means,**{metric:.79})
            self.assertFalse(all(runner.gates(self.config,totals,paired,means,worse).values()))

    def test_missing_or_nonfinite_metrics_cannot_pass(self):
        totals,paired,means=self.passing_gates()
        for value in ({},dict(means,beat_f1=float("nan"))):
            with self.assertRaises(ValueError):
                runner.gates(self.config,totals,paired,means,value)
        del paired["coarse"]["joint_breaks"]
        with self.assertRaises(ValueError):
            runner.gates(self.config,totals,paired,means,means)

    def test_event_epsilon_uses_fraction_units_and_unrounded_means(self):
        totals,paired,means=self.passing_gates()
        close={k:v-5e-13 for k,v in means.items()}
        self.assertTrue(all(runner.gates(self.config,totals,paired,means,close).values()))

    def test_complete_generated_construction_then_evaluation_reports_rejection(self):
        import apta_1_1_neural_comparison as comparison
        dense,legacy,prepared=self.fixture()
        output,_=self.construct(dense,legacy)
        with self.patched(),mock.patch.object(comparison,"event_metrics",side_effect=metric_stub):
            result=runner.evaluate(output,legacy,prepared,self.root/"evaluation")
        self.assertTrue(result["integrity_passed"])
        self.assertFalse(result["development_passed"])
        self.assertEqual(result["status"],"REJECTED")
        self.assertEqual(result["totals"]["candidate_nominal"]["windows"],4)
        self.assertEqual(result["totals"]["candidate_nominal"]["reference_complete_windows"],4)
        self.assertFalse(result["confidence_calibrated"])
        self.assertFalse(result["physical_p4"])
        self.assertTrue(result["all_candidates_frozen_before_label_loading"])

    def test_evaluate_reads_no_labels_when_watchdog_completion_invalid(self):
        dense,legacy,prepared=self.fixture(count=1)
        output,_=self.construct(dense,legacy)
        (Path(str(output)+"-supervision")/"report.json").unlink()
        with self.patched(),mock.patch.object(runner,"warm_evaluation_runtime",side_effect=AssertionError("no labels/runtime yet")):
            with self.assertRaises(FileNotFoundError):
                runner.evaluate(output,legacy,prepared,self.root/"bad-eval")

    def test_parent_watchdog_timeout_and_output_limits_reject_and_preserve_logs(self):
        for name,command,seconds,byte_limit in (
            ("timeout",[sys.executable,"-c","import time; time.sleep(5)"],.15,100000),
            ("bytes",[sys.executable,"-c","print('x'*200000)"],5,10000)):
            output=self.root/name
            output.mkdir()
            with self.assertRaises(ValueError):
                io.supervise(command,seconds,262144,byte_limit,(output,),output/"worker")
            self.assertTrue((output/"worker.stderr").exists())
            self.assertEqual(json.loads((output/"worker.resources.json").read_text())["status"],"INVALID")

    def test_parent_watchdog_includes_own_and_child_rss_and_rejects_child_overage(self):
        output=self.root/"rss"
        output.mkdir()
        limit=runner.rss_check(1024*1024)["self_peak_rss_kib"]+32768
        allocation=(limit+16384)*1024
        command=[sys.executable,"-c","import time; b=bytearray("+str(allocation)+"); time.sleep(5)"]
        with self.assertRaises(ValueError):
            io.supervise(command,5,limit,100000,(output,),output/"worker")
        result=json.loads((output/"worker.resources.json").read_text())
        self.assertGreater(result["max_observed_process_rss_kib"],limit)
        self.assertIn(str(os.getpid()),result["peak_rss_kib_by_pid"])

    def test_parent_watchdog_monitors_grandchild_process_group(self):
        output=self.root/"grandchild"
        output.mkdir()
        limit=runner.rss_check(1024*1024)["self_peak_rss_kib"]+32768
        child="import time; b=bytearray("+str((limit+16384)*1024)+"); time.sleep(5)"
        command=[sys.executable,"-c","import subprocess,sys,time; subprocess.Popen([sys.executable,'-c',"+repr(child)+"]); time.sleep(5)"]
        with self.assertRaises(ValueError):
            io.supervise(command,5,limit,100000,(output,),output/"worker")
        result=json.loads((output/"worker.resources.json").read_text())
        self.assertGreaterEqual(len(result["peak_rss_kib_by_pid"]),3)

    def test_failed_worker_or_orphan_is_invalid_not_a_musical_verdict(self):
        orphan="import subprocess,sys,time; subprocess.Popen([sys.executable,'-c','import time; time.sleep(5)']); time.sleep(.1)"
        for name,code in (("nonzero","import sys; sys.exit(7)"),("orphan",orphan)):
            output=self.root/name
            output.mkdir()
            with self.assertRaises(ValueError):
                io.supervise([sys.executable,"-c",code],5,262144,100000,(output,),output/"worker")
            result=json.loads((output/"worker.resources.json").read_text())
            self.assertEqual(result["status"],"INVALID")

    def test_supervisor_own_report_cannot_turn_output_overflow_into_pass(self):
        output=self.root/"final-report-limit"
        output.mkdir()
        with self.assertRaises(ValueError):
            io.supervise([sys.executable,"-c","pass"],5,262144,100,(output,),output/"worker")
        report=json.loads((output/"worker.resources.json").read_text())
        self.assertEqual(report["status"],"INVALID")

    def test_real_event_helper_with_generated_data_when_runtime_available(self):
        try:
            import mir_eval
        except ImportError:
            self.skipTest("pinned mir_eval runtime required; exercised separately in DD-03 verification")
        import apta_1_1_neural_comparison as comparison
        dense,legacy,prepared=self.fixture(count=1,event_metric=comparison.event_metrics)
        candidates,_=self.construct(dense,legacy)
        with self.patched():
            result=runner.evaluate(candidates,legacy,prepared,self.root/"real-metric-eval")
        self.assertEqual(result["old_event_means"],result["candidate_event_means"])
        self.assertEqual(result["status"],"REJECTED")
        self.assertFalse(result["formal_acceptance"])

    def test_tree_bytes_counts_once_and_rejects_output_symlink(self):
        p=self.root/"data"
        p.write_bytes(b"1234")
        self.assertEqual(io.tree_bytes((self.root,self.root)),4)
        (self.root/"link").symlink_to(p)
        with self.assertRaises(ValueError):
            io.tree_bytes((self.root,))


if __name__=="__main__":
    unittest.main()
