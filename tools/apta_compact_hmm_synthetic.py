#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""HS-01 Linux supervisor. Fixed generated Rust cases only; no neural/corpus imports."""
import argparse
import hashlib
import json
import math
import resource
from pathlib import Path
import struct
import subprocess
import sys
import time

import apta_dense_runner_support as io

ROOT = Path(__file__).resolve().parents[1]
PROTOCOL = ROOT / "docs/rust/PRODUCT-HOST-HMM-PROTOCOL.json"
EXPECTED_PROTOCOL = "052ce2a59ae25f17f2bdaf93e2dd317f9593657a098c6b6c9dd69340e1c446de"
COMPONENTS = [
    "rust/runtime/Cargo.toml", "rust/runtime/src/lib.rs",
    "rust/runtime/src/compact_hmm.rs",
    "rust/runtime/src/bin/apta-compact-hmm-synthetic.rs",
    "rust/runtime/tests/compact_hmm.rs", "tools/apta_compact_hmm_synthetic.py",
]
ASSETS = ROOT / "docs/rust/fixtures/product-compact-cs02/assets"


def strict_json(raw):
    def unique(pairs):
        result = {}
        for k, v in pairs:
            if k in result:
                raise ValueError("duplicate JSON key")
            result[k] = v
        return result

    def invalid(value):
        raise ValueError("nonfinite JSON constant " + value)
    return json.loads(raw, object_pairs_hook=unique, parse_constant=invalid)


def protocol():
    if io.sha(PROTOCOL) != EXPECTED_PROTOCOL:
        raise ValueError("HS-01 registration changed")
    return strict_json(PROTOCOL.read_text())


def density_hash(rows, meter):
    h = hashlib.sha256()
    for r in range(rows):
        triple = [.02, .96, .02] if r % (25 * meter) == 0 else (
            [.96, .02, .02] if r % 25 == 0 else [.01, .01, .98])
        p = struct.unpack("<fff", struct.pack("<fff", *triple))
        for value in [math.log(p[2] / 15), math.log(p[0]), math.log(p[1])]:
            h.update(struct.pack("<d", value))
    return h.hexdigest()


def worker(executable, case, output):
    p = protocol()
    match = next(c for c in p["cases"] if c["name"] == case)
    limits = p["limits"]
    resource.setrlimit(resource.RLIMIT_AS, (limits["address_space_bytes"],) * 2)
    resource.setrlimit(resource.RLIMIT_CPU, (limits["cpu_seconds"],) * 2)
    started = time.monotonic()
    stdout_path, stderr_path = output.with_suffix(".child.stdout"), output.with_suffix(".child.stderr")
    with stdout_path.open("x") as stdout, stderr_path.open("x") as stderr:
        result = subprocess.run([str(executable), "--case", case], stdout=stdout, stderr=stderr)
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    if result.returncode:
        raise ValueError("Rust child failed; preserved child stdout/stderr")
    if stdout_path.stat().st_size + stderr_path.stat().st_size > limits["output_bytes"]:
        raise ValueError("child output cap exceeded")
    data = strict_json(stdout_path.read_text())
    digest = hashlib.sha256()
    for name in sorted(n for n in p["assets"] if n.startswith("hmm")):
        digest.update((ASSETS / name).read_bytes())
    if (data["case"] != case or data["status"] != "PASS"
            or data["rows"] != match["rows"]
            or data["protocol_sha256"] != EXPECTED_PROTOCOL
            or data["asset_sha256"] != digest.hexdigest()
            or data["density_sha256"] != density_hash(match["rows"], match["meter"])):
        raise ValueError("generated input, executable protocol or asset identity differs")
    if usage.ru_maxrss > limits["rss_kib"]:
        raise ValueError("child high-water RSS cap exceeded")
    if usage.ru_utime + usage.ru_stime > limits["cpu_seconds"]:
        raise ValueError("child CPU cap exceeded")
    data["child_peak_rss_kib"] = usage.ru_maxrss
    data["child_cpu_seconds"] = usage.ru_utime + usage.ru_stime
    data["worker_wall_seconds"] = time.monotonic() - started
    data["child_stdout_sha256"] = io.sha(stdout_path)
    data["child_stderr_sha256"] = io.sha(stderr_path)
    data["applied_rlimits"] = {"AS": resource.getrlimit(resource.RLIMIT_AS),
                              "CPU": resource.getrlimit(resource.RLIMIT_CPU)}
    runtime = {}
    data["worker_runtime_files"] = io.runtime_snapshot(runtime)
    data["worker_runtime_binding_sha256"] = hashlib.sha256(
        json.dumps(runtime, sort_keys=True).encode()).hexdigest()
    io.write_new(output.with_suffix(".runtime.json"), runtime)
    io.write_new(output, data)


def enforcement(output):
    """Preserve intentional failures; use the existing neutral supervisor unchanged."""
    results = {}
    commands = {
        "wall": ([sys.executable, "-c", "import time;time.sleep(1)"], .05, 131072, 1048576, "wall-time cap exceeded"),
        "output": ([sys.executable, "-c", "import sys;sys.stdout.write('x'*65536);sys.stdout.flush()"], 5, 131072, 4096, "output cap exceeded"),
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
                 10, 131072, 1048576, [folder], folder / "supervisor")
    results["rss"] = "REFUSED_AS_EXPECTED"
    folder = output / "enforcement-address"
    folder.mkdir()
    code = (
        "import resource;resource.setrlimit(resource.RLIMIT_AS,(268435456,268435456))\n"
        "try:x=bytearray(536870912)\n"
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
    parser.add_argument("--worker", choices=["small3", "small4", "full3", "full4", "cancel"])
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
    io.bind(ROOT / p["document"]["path"], p["document"]["sha256"], bindings)
    for name, digest in p["authority_sha256"].items():
        io.bind(ROOT / name, digest, bindings)
    for name, entry in p["assets"].items():
        io.bind(ASSETS / name, entry["sha256"], bindings)
    for name in COMPONENTS:
        io.bind(ROOT / name, io.sha(ROOT / name), bindings)
    io.bind(PROTOCOL, EXPECTED_PROTOCOL, bindings)
    io.bind(args.executable, io.sha(args.executable), bindings)
    io.bind(Path(sys.executable).resolve(), io.sha(Path(sys.executable).resolve()), bindings)
    # Resolve only this ELF's runtime libraries; no package/environment discovery.
    linked = subprocess.run(["ldd", str(args.executable.resolve())], capture_output=True,
                            text=True, check=True)
    for token in linked.stdout.split():
        if token.startswith("/"):
            path = Path(token).resolve()
            io.bind(path, io.sha(path), bindings)
    subprocess.run([sys.executable, "-c", "pass"], check=True, capture_output=True,
                   text=True, timeout=5)
    runtime_files = io.runtime_snapshot(bindings)
    args.output.mkdir(parents=True, exist_ok=False)
    receipts = []
    final = {"format": "libapta-host-hmm-result-1", "protocol_sha256": EXPECTED_PROTOCOL,
             "source_revision": revision, "executable_sha256": io.sha(args.executable),
             "component_sha256": {n: io.sha(ROOT / n) for n in COMPONENTS},
             "python": sys.version, "parent_runtime_files": runtime_files,
             "model_corpus_or_device_executed": False}
    try:
        print("HS-01 operational enforcement preflight", flush=True)
        final["enforcement"] = enforcement(args.output)
        io.write_new(args.output / "bindings.json", bindings)
        for case in p["cases"]:
            name = case["name"]
            print("HS-01 start " + name, flush=True)
            case_path = args.output / (name + ".json")
            command = [sys.executable, str(Path(__file__).resolve()), "--worker", name,
                       "--executable", str(args.executable.resolve()), "--output", str(case_path)]
            limits = p["limits"]
            resources = io.supervise(command, limits["wall_seconds"], limits["rss_kib"],
                                     limits["output_bytes"], [args.output], args.output / name)
            data = strict_json(case_path.read_text())
            if data["status"] != "PASS":
                raise ValueError("worker result incomplete")
            receipts.append({"case": name, "result": data, "supervision": resources})
            io.recheck(bindings)
            io.runtime_recheck(bindings)
            print("HS-01 PASS " + name + " " + str(round(data["worker_wall_seconds"], 3)) + "s", flush=True)
        io.check_source(ROOT, revision)
        final.update(status="HS01_SYNTHETIC_HOST_PASS_ONLY", cases=receipts)
    except BaseException as exc:
        final.update(status="INVALID_OR_INCOMPLETE", failure=str(exc), cases=receipts)
        io.write_new(args.output / "result.json", final)
        raise
    io.write_new(args.output / "result.json", final)
    print(json.dumps({"status": final["status"], "cases": len(receipts)}), flush=True)


if __name__ == "__main__":
    main()
