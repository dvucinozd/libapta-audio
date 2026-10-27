# SPDX-License-Identifier: Apache-2.0
"""Integrity/unknown/operational checks; all inputs are explicitly synthetic."""
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import apta_target_probe_telemetry as tool


def manifest():
    return dict(schema=1, identity=dict(run_id="synthetic-run", unit_id="synthetic-unit",
                board="JC1060P470C_I_W_Y", image_id="synthetic-image", elf_sha256="a"*64,
                source_revision="b"*40, protocol_sha256="c"*64, chip_revision=302,
                evidence_kind="synthetic"), clock_hz=1000000, counter_bits=32,
                observations=[dict(id="baseline-0", case="TP-01", variant="resident", trial=0,
                                   phase="sample", max_elapsed_ms=100)])


def observation(m=None):
    m = manifest() if m is None else m
    plan = m["observations"][0]
    return dict(schema=1, kind="observation", sequence=0, identity=copy.deepcopy(m["identity"]),
                observation_id=plan["id"], case=plan["case"], variant=plan["variant"],
                trial=plan["trial"], phase=plan["phase"], status="MEASURED",
                ticks_start=1000, ticks_end=2250, elapsed_ticks=1250, clock_hz=m["clock_hz"],
                memory={name: dict(capacity_bytes=1000, used_bytes=400, free_bytes=600,
                                  minimum_free_bytes=500, largest_free_block_bytes=512)
                        for name in ["internal", "psram", "dma"]},
                stack=dict(capacity_bytes=4096, high_water_used_bytes=1536),
                counters=dict(audio_underruns=0, usb_errors=0), result_sha256=None)


def end(m=None, count=1):
    m = manifest() if m is None else m
    return dict(schema=1, kind="end", sequence=count, identity=copy.deepcopy(m["identity"]), observations=count)


def line(record):
    return tool.FORMAT.encode() + json.dumps(record, separators=(",", ":")).encode() + b"\n"


def run(record=None, m=None):
    m = manifest() if m is None else m
    return tool.analyze(line(observation(m) if record is None else record) + line(end(m)), m)


class TelemetryTests(unittest.TestCase):
    def test_published_synthetic_example_matches_documented_unknown_capture(self):
        folder = Path(__file__).resolve().parents[2] / "docs/rust/fixtures/target-probe-telemetry-rc04a"
        m = tool.strict_json((folder/"example-manifest.json").read_bytes())
        r = tool.analyze((folder/"example.jsonl").read_bytes(), m)
        self.assertEqual(r["status"], "VALID_WITH_UNKNOWN_METRICS")
        self.assertEqual(r["completed_observations"], 2)
        self.assertEqual(r["unknown_metrics"], 1)
        self.assertEqual(r["evidence_kind"], "synthetic")
        self.assertEqual(r["qualification"], "NOT_QUALIFIED")
        self.assertFalse(r["physical_measurement_verified"])

    def test_complete_log_is_integrity_only_and_preserves_integer_time(self):
        r = run()
        self.assertEqual(r["status"], "VALID_COMPLETE")
        self.assertEqual(r["completed_observations"], 1)
        self.assertEqual(r["observations"][0]["elapsed_ticks"], 1250)
        self.assertEqual(r["last_complete_sequence"], 1)
        self.assertEqual(r["experiment_verdict"], "NOT_EVALUATED")
        self.assertEqual(r["qualification"], "NOT_QUALIFIED")
        self.assertFalse(r["physical_measurement_verified"])
        self.assertFalse(r["programmed_image_verified"])
        self.assertNotIn("memory_total_bytes", r)  # DMA may alias internal RAM.

    def test_unknown_memory_stack_and_counters_stay_unknown(self):
        obs = observation()
        for mem in obs["memory"].values():
            for k in mem:
                mem[k] = None
        obs["stack"] = dict.fromkeys(obs["stack"], None)
        obs["counters"] = dict.fromkeys(obs["counters"], None)
        r = run(obs)
        self.assertEqual(r["status"], "VALID_WITH_UNKNOWN_METRICS")
        self.assertEqual(r["unknown_metrics"], 19)
        self.assertIsNone(obs["memory"]["psram"]["free_bytes"])

    def test_failed_unsupported_cancelled_are_retained_not_promoted(self):
        for status in ["FAILED", "NOT_SUPPORTED", "CANCELLED"]:
            with self.subTest(status=status):
                obs = observation()
                obs["status"] = status
                obs["counters"]["usb_errors"] = 3
                r = run(obs)
                self.assertEqual(r["status"], "VALID_COMPLETE")
                self.assertEqual(r["failed_or_unsupported_observations"], ["baseline-0"])
                self.assertEqual(r["observations_with_error_counters"], ["baseline-0"])
                self.assertEqual(r["experiment_verdict"], "NOT_EVALUATED")

    def test_every_identity_field_is_bound_and_synthetic_cannot_become_device(self):
        changes = dict(run_id="different", unit_id="another", board="JC8012P4A1C_I_W_Y",
                       image_id="different", elf_sha256="d"*64, source_revision="d"*40,
                       protocol_sha256="d"*64, chip_revision=303, evidence_kind="device_log")
        for k, value in changes.items():
            with self.subTest(field=k):
                obs = observation()
                obs["identity"][k] = value
                self.assertEqual(run(obs)["status"], "INVALID")
        obs = observation()
        obs["identity"]["chip_revision"] = True
        self.assertEqual(run(obs)["status"], "INVALID")

    def test_record_and_nested_fields_are_exact(self):
        for location in [None, "identity", "stack", "counters", "memory"]:
            for mode in ["extra", "missing"]:
                with self.subTest(location=location, mode=mode):
                    obs = observation()
                    target = obs if location is None else obs[location]
                    if mode == "extra":
                        target["unexpected"] = 0
                    else:
                        target.pop(next(iter(target)))
                    self.assertEqual(run(obs)["status"], "INVALID")
        for mode in ["extra", "missing"]:
            obs = observation()
            mem = obs["memory"]["internal"]
            if mode == "extra":
                mem["peak"] = 1
            else:
                del mem["free_bytes"]
            self.assertEqual(run(obs)["status"], "INVALID")

    def test_memory_inconsistency_and_integer_domains_refused(self):
        for k, v in [("free_bytes", 601), ("used_bytes", -1), ("free_bytes", True),
                     ("capacity_bytes", tool.U64 + 1), ("minimum_free_bytes", 601),
                     ("largest_free_block_bytes", 601)]:
            with self.subTest(field=k, value=v):
                obs = observation()
                obs["memory"]["internal"][k] = v
                self.assertEqual(run(obs)["status"], "INVALID")
        obs = observation()
        obs["stack"]["high_water_used_bytes"] = 4097
        self.assertEqual(run(obs)["status"], "INVALID")
        obs = observation()
        obs["counters"]["usb_errors"] = -1
        self.assertEqual(run(obs)["status"], "INVALID")

    def test_partial_metrics_are_not_filled_from_capacity(self):
        obs = observation()
        obs["memory"]["psram"]["free_bytes"] = None
        r = run(obs)
        self.assertEqual(r["status"], "VALID_WITH_UNKNOWN_METRICS")
        self.assertEqual(r["unknown_metrics"], 1)

    def test_time_wrap_cap_and_clock_validation(self):
        for bits in [32, 64]:
            m = manifest()
            m["counter_bits"] = bits
            obs = observation(m)
            obs.update(ticks_start=(1 << bits)-100, ticks_end=50, elapsed_ticks=150)
            self.assertEqual(run(obs, m)["status"], "VALID_COMPLETE")
        for change in [dict(ticks_end=2251), dict(elapsed_ticks=-1), dict(ticks_start=True),
                       dict(clock_hz=999999), dict(elapsed_ticks=100001, ticks_end=101001),
                       dict(elapsed_ticks=1 << 32)]:
            with self.subTest(change=change):
                obs = observation()
                obs.update(change)
                self.assertEqual(run(obs)["status"], "INVALID")

    def test_schema_status_digest_and_plan_coordinates(self):
        for change in [dict(schema=True), dict(schema=2), dict(sequence=True), dict(sequence=1),
                       dict(kind="boot"), dict(status="PASS"), dict(result_sha256="D"*64),
                       dict(case="TP-99"), dict(variant="other"), dict(phase="final"),
                       dict(trial=True), dict(trial=1), dict(observation_id="different")]:
            with self.subTest(change=change):
                obs = observation()
                obs.update(change)
                self.assertEqual(run(obs)["status"], "INVALID")

    def test_empty_partial_and_missing_terminal_preserve_accepted_prefix(self):
        self.assertEqual(tool.analyze(b"", manifest())["status"], "INCOMPLETE")
        for raw in [line(observation()), line(observation()) + line(end())[:-3]]:
            r = tool.analyze(raw, manifest())
            self.assertEqual(r["status"], "INCOMPLETE")
            self.assertEqual(r["completed_observations"], 1)
            self.assertEqual(r["last_complete_sequence"], 0)

    def test_duplicate_sequence_extra_record_wrong_end_and_noise(self):
        raw_obs, raw_end = line(observation()), line(end())
        for raw in [raw_obs + raw_obs + raw_end, raw_obs + raw_end + raw_end,
                    raw_obs + raw_end + raw_end[:-1],
                    b"boot noise\n" + raw_obs + raw_end, raw_obs + raw_end + b"x\n",
                    raw_obs + line(end(count=0)), raw_end, raw_obs + b"\n"]:
            with self.subTest(raw=raw[:80]):
                self.assertEqual(tool.analyze(raw, manifest())["status"], "INVALID")

    def test_strict_json_nonfinite_float_duplicate_utf8_and_deep_payload(self):
        good = line(observation())
        variants = [good.replace(b'"schema":1', b'"schema":1,"schema":1', 1),
                    good.replace(b'"elapsed_ticks":1250', b'"elapsed_ticks":NaN'),
                    good.replace(b'"elapsed_ticks":1250', b'"elapsed_ticks":Infinity'),
                    good.replace(b'"elapsed_ticks":1250', b'"elapsed_ticks":1.0'),
                    good.replace(b'"elapsed_ticks":1250', b'"elapsed_ticks":'+b'9'*100),
                    tool.FORMAT.encode() + b'{"x":"\xff"}\n',
                    tool.FORMAT.encode() + b'['*1500 + b'0'+ b']'*1500+b'\n']
        for raw in variants:
            with self.subTest(raw=raw[:40]):
                self.assertEqual(tool.analyze(raw, manifest())["status"], "INVALID")

    def test_log_and_line_caps_refuse_and_do_not_accept_a_prefix_as_complete(self):
        r = tool.analyze(b"x" * (tool.MAX_LOG + 1), manifest())
        self.assertEqual(r["status"], "INVALID")
        r = tool.analyze(tool.FORMAT.encode() + b"x"*tool.MAX_LINE + b"\n", manifest())
        self.assertEqual(r["status"], "INVALID")

    def test_manifest_refuses_unknown_duplicate_unbounded_or_ambiguous_plan(self):
        for change in [dict(schema=True), dict(clock_hz=999), dict(clock_hz=True),
                       dict(counter_bits=31), dict(observations=[]), dict(unknown=1)]:
            with self.subTest(change=change):
                m = manifest()
                m.update(change)
                with self.assertRaises(tool.Invalid):
                    tool.validate_manifest(m)
        for field, value in [("id", "space here"), ("max_elapsed_ms", 0), ("trial", True), ("case", "TP-07")]:
            m = manifest()
            m["observations"][0][field] = value
            with self.assertRaises(tool.Invalid):
                tool.validate_manifest(m)
        m = manifest()
        m["observations"] *= 2
        with self.assertRaises(tool.Invalid):
            tool.validate_manifest(m)
        m["observations"] *= tool.MAX_OBSERVATIONS
        with self.assertRaises(tool.Invalid):
            tool.validate_manifest(m)
        m = manifest()
        m["clock_hz"] = 1000000000
        m["observations"][0]["max_elapsed_ms"] = 5000
        with self.assertRaises(tool.Invalid):
            tool.validate_manifest(m)

    def test_multiple_ordered_observations_and_unknown_second_record(self):
        m = manifest()
        other = copy.deepcopy(m["observations"][0])
        other.update(id="baseline-1", trial=1)
        m["observations"].append(other)
        first = observation(m)
        second = copy.deepcopy(first)
        second.update(sequence=1, observation_id="baseline-1", trial=1)
        second["stack"]["high_water_used_bytes"] = None
        r = tool.analyze(line(first)+line(second)+line(end(m, 2)), m)
        self.assertEqual(r["status"], "VALID_WITH_UNKNOWN_METRICS")
        self.assertEqual(r["completed_observations"], 2)
        self.assertEqual(r["unknown_metrics"], 1)

    def test_offline_ingest_preserves_raw_inputs_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            mp, lp, output = p/"input.json", p/"input.log", p/"new"
            mp.write_bytes(tool.json_bytes(manifest()))
            raw = line(observation()) + line(end())
            lp.write_bytes(raw)
            result = tool.ingest(mp, lp, output)
            self.assertEqual(result["status"], "VALID_COMPLETE")
            self.assertEqual((output/"raw.log").read_bytes(), raw)
            self.assertEqual(result["retained_raw_log_sha256"], hashlib.sha256(raw).hexdigest())
            self.assertEqual((output/"manifest.json").read_bytes(), mp.read_bytes())
            before = (output/"result.json").read_bytes()
            with self.assertRaises(tool.Invalid):
                tool.ingest(mp, lp, output)
            self.assertEqual((output/"result.json").read_bytes(), before)

    def test_oversized_inputs_are_bounded_retained_prefixes_with_invalid_receipt(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            mp, lp = p/"input.json", p/"input.log"
            mp.write_bytes(tool.json_bytes(manifest()))
            lp.write_bytes(b"x"*(tool.MAX_LOG+100))
            r = tool.ingest(mp, lp, p/"large-log")
            self.assertEqual(r["status"], "INVALID")
            self.assertTrue(r["log_truncated_at_cap"])
            self.assertEqual((p/"large-log/raw.log").stat().st_size, tool.MAX_LOG+1)
            mp.write_bytes(b"x"*(tool.MAX_MANIFEST+100))
            r = tool.ingest(mp, lp, p/"large-manifest")
            self.assertEqual(r["status"], "INVALID")
            self.assertTrue(r["manifest_truncated_at_cap"])
            self.assertEqual((p/"large-manifest/manifest.json").stat().st_size, tool.MAX_MANIFEST+1)

    def test_cli_valid_and_incomplete_exit_codes_and_arbitrary_argument_refusal(self):
        script = Path(tool.__file__)
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            mp, lp = p/"manifest.json", p/"raw.log"
            mp.write_bytes(tool.json_bytes(manifest()))
            for expected, raw in [(0, line(observation())+line(end())), (2, line(observation()))]:
                lp.write_bytes(raw)
                result = subprocess.run([sys.executable, str(script), "--manifest", str(mp),
                                         "--log", str(lp), "--output", str(p/str(expected))],
                                        capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, expected, result.stderr)
                self.assertEqual(json.loads(result.stdout)["qualification"], "NOT_QUALIFIED")
            result = subprocess.run([sys.executable, str(script), "--flash", "COM4"],
                                    capture_output=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout, b"")


if __name__ == "__main__":
    unittest.main()
