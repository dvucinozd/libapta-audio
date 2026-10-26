#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""HS-02 fixed synthetic postprocessor supervisor. No model/corpus imports."""
import argparse
import hashlib
import json
from pathlib import Path
import resource
import struct
import subprocess
import sys
import time
import apta_dense_runner_support as io

ROOT = Path(__file__).resolve().parents[1]
PROTOCOL = ROOT / "docs/rust/PRODUCT-HOST-GRID-PROTOCOL.json"
EXPECTED_PROTOCOL = "282a8e78970aaf59c1ad837c84713c7d1ddb1dc5b87b1a30a0d1b56d63389e7e"
CASES = ["fixtures", "full3-44100", "full4-44100", "full3-48000", "full4-48000", "cancel"]


def strict_json(raw):
    def unique(pairs):
        out = {}
        for key, value in pairs:
            if key in out:
                raise ValueError("duplicate JSON key")
            out[key] = value
        return out
    def invalid(value):
        raise ValueError("nonfinite JSON constant " + value)
    return json.loads(raw, object_pairs_hook=unique, parse_constant=invalid)


def protocol():
    if io.sha(PROTOCOL) != EXPECTED_PROTOCOL:
        raise ValueError("HS02 registration changed")
    return strict_json(PROTOCOL.read_text())


def expand_rows(value):
    return [value["first"] + j * value["step"] for j in range(value["count"])] if isinstance(value, dict) else value


def input_hash(c):
    full = "peak_count" in c
    path = full or c["kind"] == "path"
    span = c["decoded_span"] if path else [0, 0]
    h = hashlib.sha256(struct.pack("<IQBII", c["sample_rate"], c["total_frames"], c["meter"], *span))
    if path:
        p = c["probability_recipe"] if full else c["probabilities"]
        if full or isinstance(p, dict) and "peak_train" in p:
            recipe = p if full else p["peak_train"]
            count = c["source_rows"] if full else p["count"]
            first = c["first_peak_row"] if full else recipe["first"]
            interval = c["interval"] if full else recipe["interval"]
            peaks = c["peak_count"] if full else recipe["count"]
            start = c["start_number"] if full else recipe["start_number"]
            probabilities = [recipe["other"]] * count
            for j in range(peaks):
                probabilities[first + j * interval] = recipe["downbeat"] if (start - 1 + j) % c["meter"] == 0 else recipe["beat"]
        elif isinstance(p, dict):
            probabilities = [p["constant"]] * p["count"]
        else:
            probabilities = p
        for row in probabilities:
            for bits in row:
                h.update(int(bits, 16).to_bytes(4, "little"))
        recipe = c["state_recipe"] if full else c["states"]
        if "literal_ids" in recipe:
            states = recipe["literal_ids"]
        else:
            t = recipe["tuple_progression"]
            interval = t["interval"]
            states = []
            for k in range(span[1] - span[0]):
                v = t["initial_q"] + k
                b = (t["initial_beat_index"] + v // interval) % c["meter"]
                states.append(b * 2805 + (interval - 10) * (interval + 9) // 2 + v % interval)
        for state in states:
            h.update(struct.pack("<H", state))
    else:
        rows = expand_rows(c["rows"])
        n = c["beat_numbers"]
        numbers = [((n["first"] - 1 + j) % n["cycle"]) + 1 for j in range(n["count"])] if isinstance(n, dict) else n
        for j, (row, number) in enumerate(zip(rows, numbers)):
            cl = c["classes"][j] if isinstance(c["classes"], list) else int(number == 1)
            h.update(struct.pack("<IBBBB", row, number, cl, *c["reserved_bytes"]))
    return h.hexdigest()


def worker(executable, name, output):
    p = protocol()
    fixture = ROOT / p["fixtures"]["path"]
    if io.sha(fixture) != p["fixtures"]["sha256"]:
        raise ValueError("fixture identity differs")
    f = strict_json(fixture.read_text())
    limits = p["limits"]
    resource.setrlimit(resource.RLIMIT_AS, (limits["address_space_bytes"],) * 2)
    resource.setrlimit(resource.RLIMIT_CPU, (limits["cpu_seconds"],) * 2)
    start = time.monotonic()
    stdout_path, stderr_path = output.with_suffix(".child.stdout"), output.with_suffix(".child.stderr")
    with stdout_path.open("x") as out, stderr_path.open("x") as err:
        process = subprocess.run([str(executable), "--case", name], stdout=out, stderr=err)
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    if process.returncode:
        raise ValueError("Rust child failed; stdout/stderr retained")
    data = strict_json(stdout_path.read_text())
    assets = hashlib.sha256()
    for asset in sorted(p["assets"]):
        assets.update((ROOT / p["assets_root"] / asset).read_bytes())
    if (data["case"] != name or data["status"] != "COMPLETE"
            or data["protocol_sha256"] != EXPECTED_PROTOCOL
            or data["fixture_sha256"] != p["fixtures"]["sha256"]
            or data["asset_sha256"] != assets.hexdigest()):
        raise ValueError("executable/input identity mismatch")
    specs = f["cases"] if name == "fixtures" else [next(c for c in f["measured_full_cases"] if c["name"] == ("full4-48000" if name == "cancel" else name))]
    observed = data["observations"]
    if len(specs) != len(observed):
        raise ValueError("incomplete case denominator")
    for c, o in zip(specs, observed):
        expected_id = c.get("id", c.get("name")) if name != "cancel" else "cancel"
        if o["id"] != expected_id or o["input_sha256"] != input_hash(c):
            raise ValueError("case order or actual generated input differs")
        if not 0 <= o["units"] <= p["scheduling"]["maximum_total_units_per_case"]:
            raise ValueError("unit capacity exceeded")
        if o["payload_bytes"] > p["host_environment_basis"]["expected_generated_path_probability_event_payload_bytes"]:
            raise ValueError("generated payload exceeded registered owner ledger")
        if name == "cancel":
            if (o["outcome"] != {"status": "Cancelled", "result_exposed": False}
                    or o["phase_at_request"] != "CheckResiduals"
                    or not 0 <= o["cancel_seconds"] * 1000 <= limits["pending_host_cancel_request_to_return_ms"]):
                raise ValueError("pending request cancellation gate")
        else:
            if o["outcome"] != c["expected"]:
                raise ValueError("synthetic semantic mismatch: " + expected_id)
            for key in ["event", "summary"]:
                if "expected_" + key + "_sha256" in c and o[key + "_sha256"] != c["expected_" + key + "_sha256"]:
                    raise ValueError("canonical event/summary hash mismatch")
            if c["expected"]["status"] == "GridReady" and not o["native_wire_checked"]:
                raise ValueError("native/wire validation missing")
    if usage.ru_maxrss > limits["rss_kib"] or usage.ru_utime + usage.ru_stime > limits["cpu_seconds"]:
        raise ValueError("child high-water RSS/CPU cap exceeded")
    data.update(child_peak_rss_kib=usage.ru_maxrss, child_cpu_seconds=usage.ru_utime + usage.ru_stime,
                worker_wall_seconds=time.monotonic() - start,
                child_stdout_sha256=io.sha(stdout_path), child_stderr_sha256=io.sha(stderr_path),
                applied_rlimits={"AS": resource.getrlimit(resource.RLIMIT_AS), "CPU": resource.getrlimit(resource.RLIMIT_CPU)}, validated_observations=len(observed))
    runtime = {}
    data["worker_runtime_files"] = io.runtime_snapshot(runtime)
    data["worker_runtime_binding_sha256"] = hashlib.sha256(json.dumps(runtime, sort_keys=True).encode()).hexdigest()
    io.write_new(output.with_suffix(".runtime.json"), runtime)
    io.write_new(output, data)


def enforcement(output):
    """Preserve intentional failures; use the existing neutral supervisor unchanged."""
    results = {}
    commands = {
        "wall": ([sys.executable, "-c", "import time;time.sleep(1)"], .05, 65536, 1048576, "wall-time cap exceeded"),
        "output": ([sys.executable, "-c", "import sys;sys.stdout.write('x'*65536);sys.stdout.flush()"], 5, 65536, 4096, "output cap exceeded"),
    }
    for name, (command, seconds, rss, cap, expected) in commands.items():
        folder = output / ("enforcement-" + name)
        folder.mkdir()
        try:
            io.supervise(command, seconds, rss, cap, [folder], folder / "probe")
        except ValueError as exc:
            if expected not in str(exc):
                raise ValueError("wrong enforcement failure: " + str(exc)) from exc
        else:
            raise ValueError("resource violation was accepted")
        results[name] = "REFUSED_AS_EXPECTED"
    # A fresh minimal supervisor stays below16MiB, so this proves refusal of
    # the allocating child rather than merely exceeding the parent baseline.
    folder = output / "enforcement-rss"
    folder.mkdir()
    rss_code = (
        "import sys,json;from pathlib import Path;sys.path.insert(0,sys.argv[1])\n"
        "import apta_dense_runner_support as io\n"
        "folder=Path(sys.argv[2])\n"
        "try:io.supervise([sys.executable,'-S','-c',"
        "\"import time;x=bytearray(67108864);x[::4096]=b'x'*16384;time.sleep(1)\"],5,16384,1048576,[folder],folder/'probe')\n"
        "except ValueError as e:\n"
        " assert 'RSS cap exceeded' in str(e)\n"
        "else:raise RuntimeError('RSS allocation accepted')\n"
        "r=json.loads((folder/'probe.resources.json').read_text())\n"
        "assert r['peak_rss_kib_by_pid'].get(str(r['root_pid']),0)>16384\n"
        "print('CHILD_RSS_REFUSED')\n"
    )
    io.supervise([sys.executable, "-S", "-c", rss_code, str(ROOT / "tools"), str(folder)],
                 10, 65536, 1048576, [folder], folder / "supervisor")
    results["rss"] = "REFUSED_AS_EXPECTED"
    folder = output / "enforcement-address"
    folder.mkdir()
    code = (
        "import resource;resource.setrlimit(resource.RLIMIT_AS,(134217728,134217728))\n"
        "try:x=bytearray(268435456)\n"
        "except MemoryError:print('AS_ENFORCED')\n"
        "else:raise SystemExit(7)\n"
    )
    result = subprocess.run([sys.executable, "-c", code], capture_output=True, text=True, check=True)
    (folder / "stdout.txt").write_text(result.stdout)
    if result.stdout.strip() != "AS_ENFORCED":
        raise ValueError("AS enforcement did not refuse allocation")
    results["address_space"] = "REFUSED_AS_EXPECTED"
    io.write_new(output / "enforcement.json", results)
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--worker", choices=CASES)
    args = parser.parse_args()
    if args.worker:
        worker(args.executable, args.worker, args.output)
        return
    p = protocol()
    revision = io.git(ROOT, "rev-parse", "HEAD")
    io.check_source(ROOT, revision)
    io.external(args.output, ROOT)
    io.external(args.executable, ROOT)
    io.disjoint_output(args.output, [args.executable])
    bindings = {}
    for entry in [p["document"], p["fixtures"]]:
        io.bind(ROOT / entry["path"], entry["sha256"], bindings)
    for name, digest in p["authority_sha256"].items():
        io.bind(ROOT / name, digest, bindings)
    for name, entry in p["assets"].items():
        io.bind(ROOT / p["assets_root"] / name, entry["sha256"], bindings)
    for name in p["files_planned"]:
        io.bind(ROOT / name, io.sha(ROOT / name), bindings)
    io.bind(PROTOCOL, EXPECTED_PROTOCOL, bindings)
    io.bind(args.executable, io.sha(args.executable), bindings)
    io.bind(Path(sys.executable).resolve(), io.sha(Path(sys.executable).resolve()), bindings)
    linked = subprocess.run(["ldd", str(args.executable.resolve())], capture_output=True, text=True, check=True)
    for token in linked.stdout.split():
        if token.startswith("/"):
            path = Path(token).resolve()
            io.bind(path, io.sha(path), bindings)
    subprocess.run([sys.executable, "-c", "pass"], check=True, capture_output=True, text=True, timeout=5)
    runtime_count = io.runtime_snapshot(bindings)
    args.output.mkdir(parents=True, exist_ok=False)
    final = {"format": "libapta-host-grid-result-1", "protocol_sha256": EXPECTED_PROTOCOL,
             "source_revision": revision, "executable_sha256": io.sha(args.executable),
             "component_sha256": {n: io.sha(ROOT / n) for n in p["files_planned"]},
             "python": sys.version, "parent_runtime_files": runtime_count, "model_corpus_or_device_executed": False}
    receipts = []
    try:
        print("HS-02 enforcement preflight", flush=True)
        final["enforcement"] = enforcement(args.output)
        io.write_new(args.output / "bindings.json", bindings)
        for case in p["measured_cases"]:
            name = case["name"]
            print("HS-02 start " + name, flush=True)
            path = args.output / (name + ".json")
            command = [sys.executable, str(Path(__file__).resolve()), "--worker", name,
                       "--executable", str(args.executable.resolve()), "--output", str(path)]
            limits = p["limits"]
            resources = io.supervise(command, limits["wall_seconds"], limits["rss_kib"], limits["output_bytes"], [args.output], args.output / name)
            if resources["status"] != "PASS" or resources["failure"] is not None:
                raise ValueError("supervisor final refusal")
            data = strict_json(path.read_text())
            receipts.append({"case": name, "result": data, "supervision": resources})
            io.recheck(bindings)
            io.runtime_recheck(bindings)
            print("HS-02 PASS " + name + " " + str(round(data["worker_wall_seconds"], 4)) + "s", flush=True)
        io.check_source(ROOT, revision)
        final.update(status="HS02_SYNTHETIC_HOST_PASS_ONLY", cases=receipts)
        if io.tree_bytes([args.output]) + io.json_bytes(final) > p["limits"]["output_bytes"]:
            raise ValueError("final aggregate output cap exceeded")
    except BaseException as exc:
        final.update(status="INVALID_OR_INCOMPLETE", failure=str(exc), cases=receipts)
        io.write_new(args.output / "result.json", final)
        raise
    io.write_new(args.output / "result.json", final)
    print(json.dumps({"status": final["status"], "cases": len(receipts)}), flush=True)


if __name__ == "__main__":
    main()
