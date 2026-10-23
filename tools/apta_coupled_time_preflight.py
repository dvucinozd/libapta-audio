#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generated-only CT-02 preflight. No audio, label, event-file or scoring input."""
import argparse
import hashlib
import json
from pathlib import Path
import resource
import sys
import time

import apta_dense_runner_support as io

ROOT = Path(__file__).resolve().parents[1]
PROTOCOL_SHA256 = "5f23a96330e7852f9dcb21b6273acf258e7252177da9bef53ceb9e3bea1afced"
COMPONENTS = ("tools/apta_coupled_time.py", "tools/apta_coupled_time_verify.py",
              "tools/apta_coupled_time_preflight.py", "tools/apta_dense_runner_support.py")
CASES = ("affine24000", "affine25000", "affine30240", "jitter", "ramp", "step",
         "missing", "extra", "half", "double", "crossing", "endpoints", "fixed",
         "translated", "full_affine", "full_ramp", "full_bound_active",
         "empty", "short", "gap", "gap_empty")


def generated(name):
    """Fixed public formulas only; no externally supplied observations."""
    total, windows, gaps = 500000, None, []
    a = [1000 + 24000 * i for i in range(16)]
    downbeats = None
    if name.startswith("affine"):
        period = int(name[6:])
        a = [1000 + period * i for i in range(16)]
        total = 2000 + 16 * period
    elif name == "jitter":
        a = [1000 + 24000 * i + (-160 if i % 2 == 0 else 160) for i in range(16)]
        total = 400000
    elif name == "ramp":
        a = [1000 + 23000 * i + 20 * i * (i - 1) for i in range(16)]
        total = 400000
    elif name == "step":
        a = [1000]
        for i in range(1, 16):
            a.append(a[-1] + (23000 if i <= 7 else 26000))
    elif name == "missing":
        a = [1000 + 24000 * i for i in range(17) if i != 8]
        downbeats = []
    elif name == "extra":
        a.insert(8, (a[7] + a[8]) // 2)
        downbeats = []
    elif name in ("half", "double"):
        period = 48000 if name == "half" else 12000
        a = [1000 + period * i for i in range(16)]
        total = 2000 + 16 * period
    elif name == "crossing":
        a = [1000 + 24000 * i + (320 if i == 8 else 0) for i in range(17)]
        windows = [{"first_frame": 0, "end_frame": 193319},
                   {"first_frame": 193319, "end_frame": total}]
    elif name == "endpoints":
        a, total = [24000 * i for i in range(9)], 192001
    elif name == "fixed":
        a, total = list(range(9)), 9
    elif name == "translated":
        a = [1000 + 25000 * i + 524288 for i in range(16)]
        total = 400000 + 524288
        windows = [{"first_frame": 0, "end_frame": 180000 + 524288},
                   {"first_frame": 180000 + 524288, "end_frame": total}]
    elif name.startswith("full_"):
        total = 1440000
        if name == "full_affine":
            a = [1000 + 400 * i for i in range(3072)]
        elif name == "full_ramp":
            a = [1000 + 100 * i + i * (i - 1) // 10 for i in range(3072)]
        elif name == "full_bound_active":
            a = [1000 + 400 * i + (-80 if i % 2 == 0 else 80) for i in range(3072)]
        else:
            raise ValueError("unknown generated fixture")
        windows = [{"first_frame": i * 180000, "end_frame": (i + 1) * 180000} for i in range(8)]
    elif name in ("empty", "short", "gap", "gap_empty"):
        a = [] if name in ("empty", "gap_empty") else list(range(8 if name == "short" else 9))
        total = 10
        windows = [{"first_frame": 0, "end_frame": 5}, {"first_frame": 5, "end_frame": 10}]
        if name.startswith("gap"):
            gaps = [[0, 1], [1, 2]]
    else:
        raise ValueError("unknown generated fixture")
    return (a, list(range(0, len(a), 4)) if downbeats is None else downbeats,
            total, windows or [{"first_frame": 0, "end_frame": total}], gaps)


def configuration():
    docs = ROOT / "docs/rust"
    path = docs / "COUPLED-TIME-SYNTHETIC-PROTOCOL.json"
    bindings = {}
    io.bind(path, PROTOCOL_SHA256, bindings)
    protocol = json.loads(path.read_text())
    for name, digest in protocol["binding"].items():
        io.bind(docs / name, digest, bindings)
    # Constructor remains the exact CT-01 implementation, not a repaired candidate.
    receipt_path = docs / "COUPLED-TIME-CT01-20261023.json"
    io.bind(receipt_path, io.sha(receipt_path), bindings)
    receipt = json.loads(receipt_path.read_text())
    io.bind(ROOT / "tools/apta_coupled_time.py", receipt["component_sha256"]["tools/apta_coupled_time.py"], bindings)
    for name in COMPONENTS:
        io.bind(ROOT / name, io.sha(ROOT / name), bindings)
    io.bind(Path(sys.executable).resolve(), io.sha(Path(sys.executable).resolve()), bindings)
    return protocol["limits"], bindings


def source_worker(role, name, output, freeze_sha):
    if role not in ("construct", "verify") or name not in CASES:
        raise ValueError("generated worker role/case differs")
    limits, bindings = configuration()
    ceiling = limits["max_process_rss_kib"] * 1024
    resource.setrlimit(resource.RLIMIT_AS, (ceiling, ceiling))
    if role == "construct":
        import apta_coupled_time as implementation
    else:
        import apta_coupled_time_verify as implementation
    io.runtime_snapshot(bindings)
    a, downbeats, total, windows, gaps = generated(name)
    candidate_path = output / "construct" / (name + ".candidate.json")
    started = time.monotonic()
    if role == "construct":
        value = implementation.construct(a, downbeats, total, windows, gaps)
        payload = json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode("utf-8")
        with candidate_path.open("xb") as stream:
            stream.write(payload)
    else:
        frozen = io.read_bound(output / "construct/frozen.json", freeze_sha, bindings)
        record = next(row for row in frozen["cases"] if row["case"] == name)
        io.bind(candidate_path, record["candidate_sha256"], bindings)
        payload = candidate_path.read_bytes()
        value = implementation.verify_bytes(a, downbeats, total, windows, payload, record["candidate_sha256"], gaps)
    elapsed = time.monotonic() - started
    io.recheck(bindings)
    report = {"case": name, "role": role, "status": "PASS", "candidate_status": value["status"],
              "candidate_sha256": hashlib.sha256(payload).hexdigest(), "candidate_bytes": len(payload),
              "events": value["input_event_count"], "windows": len(windows),
              "sweeps": value["solution"]["sweeps"],
              "root_evaluations": value["solution"]["root_evaluations"],
              "coordinate_updates": value["solution"]["coordinate_updates"],
              "residual_frames": value["solution"]["residual_frames"],
              "wall_seconds": elapsed, "self_peak_rss_kib": resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
              "runtime_bindings_count": len(bindings), "runtime_bindings": bindings,
              "constructor_imported": "apta_coupled_time" in sys.modules,
              "verifier_imported": "apta_coupled_time_verify" in sys.modules,
              "reference_labels_loaded": False}
    if report["constructor_imported"] != (role == "construct") or report["verifier_imported"] != (role == "verify"):
        raise ValueError("phase import boundary differs")
    io.write_new(output / role / (name + ".report.json"), report)


def phase_worker(role, output, freeze_sha):
    limits, bindings = configuration()
    if role == "verify":
        io.bind(output / "construct/frozen.json", freeze_sha, bindings)
    rows = []
    for name in CASES:
        # Inherit the outer phase group, so phase timeout kills every source worker.
        supervision = io.supervise([sys.executable, str(Path(__file__).resolve()),
                                   "_source", role, name, str(output), freeze_sha],
                                  limits["max_source_seconds"], limits["max_process_rss_kib"],
                                  limits["max_attempt_output_bytes"], (output,),
                                  output / role / (name + ".source"), group=False)
        if supervision["status"] != "PASS":
            raise ValueError("invalid source supervision")
        report_path = output / role / (name + ".report.json")
        report = json.loads(report_path.read_text())
        if report["status"] != "PASS" or report["case"] != name or report["role"] != role:
            raise ValueError("source completion differs")
        if report["self_peak_rss_kib"] > limits["max_process_rss_kib"]:
            raise ValueError("source self RSS cap exceeded")
        row = {key: value for key, value in report.items() if key != "runtime_bindings"}
        row.update(source_report_sha256=io.sha(report_path),
                   supervised_wall_seconds=supervision["wall_seconds"],
                   supervised_peak_rss_kib=supervision["max_observed_process_rss_kib"])
        rows.append(row)
        io.recheck(bindings)
    result = {"status": "PASS", "role": role, "protocol_sha256": PROTOCOL_SHA256, "cases": rows}
    if io.tree_bytes((output,)) + io.json_bytes(result) > limits["max_attempt_output_bytes"]:
        raise ValueError("phase completion output cap")
    io.write_new(output / role / ("frozen.json" if role == "construct" else "verified.json"), result)


def preflight(output):
    """Supervise both complete phases; write completion only after all checks."""
    output = Path(output).resolve()
    io.external(output, ROOT)
    io.disjoint_output(output, ())
    limits, bindings = configuration()
    output.mkdir(parents=True)
    phase_results, freeze_sha = {}, "none"
    try:
        for role in ("construct", "verify"):
            (output / role).mkdir()
            result = io.supervise([sys.executable, str(Path(__file__).resolve()),
                                   "_phase", role, str(output), freeze_sha],
                                  limits["max_phase_seconds"], limits["max_process_rss_kib"],
                                  limits["max_attempt_output_bytes"], (output,), output / (role + ".phase"))
            if result["status"] != "PASS":
                raise ValueError("invalid phase supervision")
            phase_results[role] = result
            if role == "construct":
                freeze_sha = io.sha(output / "construct/frozen.json")
                io.bind(output / "construct/frozen.json", freeze_sha, bindings)
            io.recheck(bindings)
        constructed = json.loads((output / "construct/frozen.json").read_text())
        verified = json.loads((output / "verify/verified.json").read_text())
        if ([v["case"] for v in constructed["cases"]] != list(CASES)
                or [v["case"] for v in verified["cases"]] != list(CASES)
                or [v["candidate_sha256"] for v in constructed["cases"]]
                != [v["candidate_sha256"] for v in verified["cases"]]):
            raise ValueError("complete generated replay differs")
        result = {"format": "libapta-coupled-time-generated-preflight-1", "status": "PASS",
                  "protocol_sha256": PROTOCOL_SHA256,
                  "implementation_parent_revision": io.git(ROOT, "rev-parse", "HEAD"),
                  "source_scope": "component-bound generated run; no clean-source real evaluation",
                  "component_sha256": {name: io.sha(ROOT / name) for name in COMPONENTS},
                  "interpreter_sha256": io.sha(Path(sys.executable).resolve()), "python": sys.version,
                  "limits": limits, "phases": phase_results,
                  "construct_cases": constructed["cases"], "verify_cases": verified["cases"],
                  "candidate_freeze_sha256": freeze_sha,
                  "verification_sha256": io.sha(output / "verify/verified.json"),
                  "generated_preflight_passed": True, "independent_replay_passed": True,
                  "real_constructor_cli_enabled": False, "reference_labels_loaded": False,
                  "development_verdict": "NOT_RUN", "native_graph_constructed": False,
                  "physical_p4": False, "formal_acceptance": False, "production_promotion": False}
        result["attempt_bytes_before_completion"] = io.tree_bytes((output,))
        if io.tree_bytes((output,)) + io.json_bytes(result) > limits["max_attempt_output_bytes"]:
            raise ValueError("preflight completion output cap")
        io.write_new(output / "preflight.json", result)
        return result
    except BaseException as exc:
        io.write_new(output / "INVALID.json", {"status": "INVALID", "error": str(exc),
                                              "development_verdict": "NOT_RUN"})
        raise


def main():
    # Internal roles still generate all observations from the fixed case registry.
    if len(sys.argv) == 6 and sys.argv[1] == "_source":
        source_worker(sys.argv[2], sys.argv[3], Path(sys.argv[4]), sys.argv[5])
        return 0
    if len(sys.argv) == 5 and sys.argv[1] == "_phase" and sys.argv[2] in ("construct", "verify"):
        phase_worker(sys.argv[2], Path(sys.argv[3]), sys.argv[4])
        return 0
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path, help="new external generated-only evidence directory")
    args = parser.parse_args()
    result = preflight(args.output)
    print(json.dumps({"status": result["status"], "generated_cases": len(CASES),
                      "development_verdict": "NOT_RUN"}))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, OSError, KeyError, TypeError) as error:
        print(json.dumps({"status": "INVALID", "error": str(error)}), file=sys.stderr)
        sys.exit(2)
