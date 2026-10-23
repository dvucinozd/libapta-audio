#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""DD-04 generated-only independent oracle and complete subprocess preflight."""
from copy import deepcopy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import random
import resource
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import apta_dense_decoder as model
import apta_dense_decoder_verify as verifier
import apta_1_1_dense_decoder as runner
import apta_dense_runner_support as io


def module(name):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(name + ".py"))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


fixtures = module("apta_1_1_dense_decoder")
support = module("apta_1_1_dense_decoder_runner")
COMPONENTS = ("tools/apta_dense_decoder.py", "tools/apta_1_1_dense_decoder.py",
              "tools/apta_dense_runner_support.py", "tools/apta_dense_decoder_verify.py")


def full_case(name, output):
    # Standalone child: applies both the registered hard AS ceiling and watchdog.
    resource.setrlimit(resource.RLIMIT_AS, (262144 * 1024, 262144 * 1024))
    data = list(fixtures.pulse_fixture(length=1500))
    if name == "flat":
        data[:2] = [[1.] * 1501, [1.] * 1501]
    elif name == "null":
        data[:2] = [[0.] * 1501, [0.] * 1501]
    elif name == "ambiguous":
        data[:2] = [[1. if i % 25 == 0 else -1. for i in range(1501)], [0.] * 1501]
    elif name == "variable":
        steps = [25,25,26,26,27,27,28,28,29,29,30,30,29,29,28,28,27,27,26,26]
        row, positions = 10, []
        while row < 1500:
            positions.append(row)
            row += steps[(len(positions) - 1) % len(steps)]
        data = list(fixtures.pulse_fixture(length=1500, positions=positions))
    elif name != "pulse":
        raise ValueError("unknown generated case")
    data[3] = [{"first_frame": i * 180000, "end_frame": (i + 1) * 180000} for i in range(8)]
    started = time.monotonic()
    candidate = model.construct(*data)
    decode_seconds = time.monotonic() - started
    started = time.monotonic()
    verifier.verify(*data, candidate)
    verify_seconds = time.monotonic() - started
    io.write_new(output, {"case": name, "candidate_sha256": hashlib.sha256(
        (json.dumps(candidate, indent=2, sort_keys=True) + "\n").encode()).hexdigest(),
        "status": candidate["status"], "selected_meter": candidate["selected_meter"],
        "beat_count": len(candidate["events"]["beat_frames"]),
        "state_bytes": candidate["maximum_live_state_bytes"],
        "decode_seconds": decode_seconds, "verify_seconds": verify_seconds,
        "self_peak_rss_kib": resource.getrusage(resource.RUSAGE_SELF).ru_maxrss})


class IndependentTests(unittest.TestCase):
    def compare(self, data, gaps=()):
        candidate = model.construct(*data, known_gaps=gaps)
        self.assertEqual(verifier.verify(*data, candidate, gaps), candidate)
        return candidate

    def test_small_exhaustive_scores_paths_and_ties_both_meters(self):
        rng = random.Random(5904)
        for count in (1, 10, 11, 20, 24):
            for beat, down in (([0] * count, [0] * count), ([1024] * count, [1024] * count),
                               ([rng.randrange(-8, 9)*1024 for _ in range(count)],
                                [rng.randrange(-8, 9)*1024 for _ in range(count)])):
                for meter in (3, 4):
                    expected = fixtures.exhaustive(beat, down, count * 960, meter)
                    actual = verifier.scalar_meter(beat, down, count * 960, meter)
                    for key in expected:
                        self.assertEqual(actual[key], expected[key], (count, meter, key))

    def test_hand_computed_three_beats_penalty_and_traceback(self):
        # d10 -> d11 -> d12, emissions 16384 each, two penalties 2048.
        beat, down = [-32768] * 25, [0] * 25
        for row in (0, 11, 23):
            beat[row] = 16384
        for meter in (3, 4):
            actual = verifier.scalar_meter(beat, down, 25 * 960, meter)
            self.assertEqual(actual["beat_rows"], [0, 11, 23])
            # The latent seed chooses 11 to avoid the first penalty.
            self.assertEqual(actual["incoming_intervals"], [11, 11, 12])
            self.assertEqual(actual["terminal_score"], 3 * 16384 - 2048)

    def test_independent_numerics_and_invalid_f32(self):
        tiny = struct.unpack("<f", struct.pack("<I", 1))[0]
        largest = struct.unpack("<f", struct.pack("<I", 0x7f7fffff))[0]
        for value in (0., -0., tiny, -tiny, 1/2048, -1/2048, 3/2048, -3/2048, 32., -32., largest, -largest):
            self.assertEqual(verifier.quantized(value), model.quantize(value))
        for value in (True, None, "1", 1/3, float("nan"), float("inf"), -float("inf"), 10**500):
            with self.subTest(value=repr(value)), self.assertRaises(ValueError):
                verifier.quantized(value)

    def test_exact_pulses_both_meters(self):
        for meter in (3, 4):
            result = self.compare(fixtures.pulse_fixture(meter=meter))
            self.assertEqual(result["selected_meter"], meter)
            self.assertEqual(result["events"]["beat_rows"], list(range(10, 400, 25)))

    def test_existing_generated_constructor_fixtures_replay_independently(self):
        original = model.construct
        checked = []

        def construct(beat, downbeat, total, windows, known_gaps=()):
            actual = original(beat, downbeat, total, windows, known_gaps)
            expected = verifier.verify(beat, downbeat, total, windows, actual, known_gaps)
            canonical = lambda value: (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()
            self.assertEqual(hashlib.sha256(canonical(actual)).hexdigest(), hashlib.sha256(canonical(expected)).hexdigest())
            checked.append(True)
            return actual

        names = ("equal_meter_score_selects_three_without_margin_gate", "exact_four_four_pulses_and_same_sequence_downbeats",
                 "exact_three_four_pulses", "variable_timing_is_not_forced_to_constant_period",
                 "nonpositive_path_has_empty_events_and_preserves_windows", "positive_winner_with_fewer_than_nine_events_abstains",
                 "exact_nine_event_minimum_is_supported", "endpoint_row_never_emitted_or_used_as_refinement_neighbor",
                 "last_in_domain_peak_never_uses_boundary_neighbor", "beyond_source_center_and_clipped_raw_endpoint_are_retained_as_metadata",
                 "refined_event_changes_window_once_and_retains_ordinal", "known_gap_abstains_for_whole_source_without_dp",
                 "known_gap_in_one_window_abstains_for_all_windows", "constructor_is_pure_and_does_not_mutate_inputs")
        with mock.patch.object(model, "construct", side_effect=construct):
            for name in names:
                getattr(fixtures.DenseDecoderTests("test_"+name), "test_"+name)()
        self.assertEqual(len(checked), 18)

    def test_abstention_and_gap_metadata(self):
        for data, gaps in ((fixtures.pulse_fixture(length=100), ()),
                           (fixtures.pulse_fixture(), ((100, 101),))):
            result = self.compare(data, gaps)
            self.assertFalse(result["events"]["beat_frames"])
            self.assertEqual(len(result["windows"]), len(data[3]))
        data = list(fixtures.pulse_fixture())
        for value in (0., -1.):
            data[:2] = [[value] * 401, [value] * 401]
            self.assertEqual(self.compare(data)["status"], "nonpositive_path")

    def test_refinement_cross_window_endpoint_and_subrow_geometry(self):
        data = list(fixtures.pulse_fixture())
        row = 35
        data[0][row - 1:row + 2] = [0., 8., 4.]
        data[0][-1] = 32.
        boundary = row * 960 + 1
        data[3] = [{"first_frame": 0, "end_frame": boundary}, {"first_frame": boundary, "end_frame": data[2]}]
        result = self.compare(data)
        self.assertEqual(result["events"]["beat_frames"][1], row * 960 + 160)
        self.assertEqual(result["window_ownership_changes"], [{"ordinal": 1, "row_window": 0, "refined_window": 1}])
        self.assertEqual(result["boundary_rows_excluded"], [400])
        for total in (1, 959, 961, 1920, 1921):
            rows = total * 22050 // 48000 // 441 + 1
            self.compare(([0.] * rows, [0.] * rows, total, [{"first_frame": 0, "end_frame": total}]))

    def test_geometry_schema_rejections(self):
        data = fixtures.pulse_fixture()
        for windows in ([dict(data[3][0], meter=4)], [{"first_frame": 1, "end_frame": data[2]}],
                        [{"first_frame": 0, "end_frame": data[2]-1}], [],
                        [{"first_frame": False, "end_frame": data[2]}]):
            with self.assertRaises(ValueError):
                verifier.reconstruct(*data[:3], windows)

    def test_saved_field_corruption_is_rejected(self):
        data = fixtures.pulse_fixture()
        original = model.construct(*data)
        for change in (lambda c: c.update(confidence=False), lambda c: c.update(extra=0),
                       lambda c: c["events"]["beat_frames"].__setitem__(0, 0),
                       lambda c: c["per_meter_diagnostics"][0]["incoming_intervals"].__setitem__(0, 12),
                       lambda c: c["per_meter_diagnostics"][1].update(terminal_score=0),
                       lambda c: c["windows"][0]["beat_ordinals"].pop(),
                       lambda c: c.update(selected_meter=3)):
            candidate = deepcopy(original)
            change(candidate)
            with self.assertRaises(ValueError):
                verifier.verify(*data, candidate)

    def test_no_production_helpers_or_evaluation_imports(self):
        code = ("import sys;sys.path.insert(0,"+repr(str(ROOT/"tools"))+ ");"
                "import apta_dense_decoder_verify;assert not any(n in sys.modules for n in "
                "['apta_dense_decoder','apta_1_1_dense_decoder','numpy','torch','apta_1_1_neural_comparison'])")
        subprocess.run([sys.executable, "-c", code], check=True, timeout=10)
        data = fixtures.pulse_fixture(length=20)
        candidate = model.construct(*data)
        with mock.patch.object(model, "construct", side_effect=AssertionError), mock.patch.object(model, "solve_meter", side_effect=AssertionError), mock.patch.object(model, "quantize", side_effect=AssertionError), mock.patch.object(model, "localize", side_effect=AssertionError):
            verifier.verify(*data, candidate)

    def test_full_size_generated_resource_cases(self):
        rows = []
        with tempfile.TemporaryDirectory(prefix="libapta-dd04-full-") as temp:
            root = Path(temp)
            for name in ("pulse", "flat", "null", "ambiguous", "variable"):
                output = root / (name + ".json")
                supervision = io.supervise([sys.executable, str(Path(__file__).resolve()), "--generated-case", name, str(output)],
                    60, 262144, 67108864, (root,), root / name)
                value = json.loads(output.read_text())
                self.assertEqual(value["state_bytes"], 3564000)
                self.assertLess(value["self_peak_rss_kib"], 262144)
                value["supervisor_max_observed_peak_rss_kib"] = supervision["max_observed_process_rss_kib"]
                rows.append(value)
        if os.environ.get("APTA_DD04_GENERATED_REPORT"):
            io.write_new(Path(os.environ["APTA_DD04_GENERATED_REPORT"]), {"cases": rows})


class OperationalTests(unittest.TestCase):
    setUp = support.RunnerTests.setUp
    fixture = support.RunnerTests.fixture
    patched = support.RunnerTests.patched

    def cli(self, role, *args):
        return subprocess.run([sys.executable, str(Path(__file__).with_name("apta_dense_preflight_bootstrap.py")),
            str(self.settings), role, *map(str, args)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=90)

    def setup_cli(self):
        import apta_1_1_neural_comparison as comparison
        dense, legacy, prepared = self.fixture(count=2, event_metric=comparison.event_metrics)
        source = self.root / "generated-source"
        source.mkdir()
        (source / "generated.txt").write_text("generated source identity\n")
        for args in (("init",), ("add", "generated.txt"),
                     ("-c", "user.name=Generated test", "-c", "user.email=generated@example.invalid", "commit", "-m", "generated fixture")):
            subprocess.run(["git", "-C", str(source), *args], check=True, capture_output=True)
        preflight = self.root / "generated-preflight.json"
        io.write_new(preflight, {"protocol_sha256": model.PROTOCOL_SHA256,
            **{k:True for k in ("generated_preflight_passed", "independent_verifier_passed", "runner_isolation_passed", "resource_enforcement_passed")},
            "component_sha256": {name:io.sha(ROOT/name) for name in COMPONENTS}})
        self.settings = self.root / "settings.json"
        io.write_new(self.settings, {"protocol": self.config, "preflight": str(preflight), "source": str(source), "components": COMPONENTS})
        return dense, legacy, prepared, source

    def test_complete_generated_cli_construct_verify_evaluate_and_stale_rejection(self):
        dense, legacy, prepared, source = self.setup_cli()
        manifest = prepared / "manifest.private.json"
        hidden_manifest = self.root / "absent-generated-labels.json"
        manifest.rename(hidden_manifest)
        candidates = self.root / "cli-candidates"
        result = self.cli("runner", "construct", "--dense", dense, "--legacy", legacy, "--output", candidates)
        self.assertEqual(result.returncode, 0, result.stderr)
        # No verifier report: evaluation must fail before labels or scoring output.
        output = self.root / "missing-verification-eval"
        result = self.cli("runner", "evaluate", "--candidates", candidates, "--legacy", legacy, "--prepared", prepared, "--output", output)
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertFalse(output.exists())
        result = self.cli("verifier", "--candidates", candidates, "--legacy", legacy)
        self.assertEqual(result.returncode, 0, result.stderr)
        hidden_manifest.rename(manifest)
        result = self.cli("runner", "evaluate", "--candidates", candidates, "--legacy", legacy, "--prepared", prepared, "--output", self.root/"cli-evaluation")
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads((self.root/"cli-evaluation/report.private.json").read_text())
        self.assertEqual(report["status"], "REJECTED")  # Fixed real gates; no synthetic promotion.
        self.assertEqual(report["old_event_means"], report["candidate_event_means"])
        if os.environ.get("APTA_DD04_OPERATIONAL_REPORT"):
            values = {}
            for phase, path in (("construct", candidates), ("verify", Path(str(candidates)+"-verification")), ("evaluate", self.root/"cli-evaluation")):
                envelope = json.loads((Path(str(path)+"-supervision")/"report.json").read_text())
                values[phase] = {k:envelope[k] for k in ("status", "wall_seconds", "max_observed_process_rss_kib", "poll_seconds", "process_group_monitored")}
            roots = (candidates, Path(str(candidates)+"-supervision"), Path(str(candidates)+"-verification"),
                     Path(str(candidates)+"-verification-supervision"), self.root/"cli-evaluation", self.root/"cli-evaluation-supervision")
            io.write_new(Path(os.environ["APTA_DD04_OPERATIONAL_REPORT"]), {"phases": values,
                "sources": 2, "registered_thresholds_unchanged": True, "generated_verdict": report["status"],
                "generated_old_and_new_event_means_equal": True, "attempt_output_bytes": io.tree_bytes(roots)})
        # Actual CLI must reject overwrite, stale source and mutated freeze.
        result = self.cli("verifier", "--candidates", candidates, "--legacy", legacy)
        self.assertEqual(result.returncode, 2)
        (source/"generated.txt").write_text("dirty\n")
        result = self.cli("runner", "construct", "--dense", dense, "--legacy", legacy, "--output", self.root/"dirty-attempt")
        self.assertEqual(result.returncode, 2)
        self.assertFalse((self.root/"dirty-attempt/candidates-frozen.private.json").exists())
        verification = Path(str(candidates)+"-verification/report.private.json")
        with self.patched(), self.assertRaises(ValueError):
            value = json.loads(verification.read_text())
            value["candidate_freeze_sha256"] = "0" * 64
            verification.write_text(json.dumps(value))
            complete = json.loads((candidates/"candidates-frozen.private.json").read_text())
            runner.validate_verification(candidates, complete, self.config, {})

    def test_public_cli_has_no_generated_identity_bypass(self):
        dense, legacy, _prepared, _source = self.setup_cli()
        output = self.root/"ordinary-cli"
        result = subprocess.run([sys.executable, str(ROOT/"tools/apta_1_1_dense_decoder.py"), "construct",
            "--dense", str(dense), "--legacy", str(legacy), "--output", str(output)],
            capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 2)
        self.assertFalse((output/"candidates-frozen.private.json").exists())

    def test_generated_cli_parent_enforces_timeout_rss_and_output(self):
        dense, legacy, _prepared, _source = self.setup_cli()
        original = json.loads(self.settings.read_text())
        for name, key, value in (("timeout", "max_construct_process_seconds", .001),
                                 ("rss", "max_host_rss_kib_per_process", 1),
                                 ("output", "max_attempt_output_bytes", 1024)):
            settings = deepcopy(original)
            settings["protocol"]["limits"][key] = value
            self.settings.write_text(json.dumps(settings))
            output = self.root/("cap-"+name)
            result = self.cli("runner", "construct", "--dense", dense, "--legacy", legacy, "--output", output)
            self.assertEqual(result.returncode, 2, result.stdout)
            self.assertFalse((output/"candidates-frozen.private.json").exists())
            report = json.loads((Path(str(output)+"-supervision")/"phase.resources.json").read_text())
            self.assertEqual(report["status"], "INVALID")
            self.assertIn({"timeout":"wall-time", "rss":"RSS", "output":"output"}[name], report["failure"])


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--generated-case":
        full_case(sys.argv[2], Path(sys.argv[3]))
    else:
        unittest.main()
