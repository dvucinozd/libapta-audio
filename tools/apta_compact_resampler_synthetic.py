#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""HS-03 fixed synthetic FIR streaming supervisor; no model/corpus imports."""
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
PROTOCOL = ROOT / "docs/rust/PRODUCT-HOST-RESAMPLER-PROTOCOL.json"
EXPECTED_PROTOCOL = "e30230da61a739b74cd5df8e15b9b0944a8a87e60074dd9729853ac20d0de9a4"
REFERENCE_ROOT = ROOT / "docs/rust/fixtures/product-host-resampler-hs03"
sys.path.insert(0, str(REFERENCE_ROOT))
import resampler_reference as reference

CASES = ["fixtures", "full44100-mono", "full44100-stereo", "full48000-mono", "full48000-stereo", "cancel"]


def strict_json(raw):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("duplicate JSON key")
            result[key] = value
        return result
    def invalid(value):
        raise ValueError("nonfinite JSON constant " + value)
    return json.loads(raw, object_pairs_hook=unique, parse_constant=invalid)


def protocol():
    if io.sha(PROTOCOL) != EXPECTED_PROTOCOL:
        raise ValueError("HS03 registration changed")
    return strict_json(PROTOCOL.read_text())


def prefix_hashes(c, frames, outputs):
    h = hashlib.sha256(struct.pack("<IBQ", c["sample_rate"], c["channels"], c["frames"]))
    tile = b"".join(struct.pack("<f", reference.signal(c, j, ch)) for j in range(257) for ch in range(c["channels"]))
    count, tail = divmod(frames, 257)
    for _ in range(count):
        h.update(tile)
    h.update(tile[:tail * c["channels"] * 4])
    out = hashlib.sha256()
    tile = reference.periodic_tile(c)
    first = (32 * 22050 + c["sample_rate"] - 1) // c["sample_rate"]
    for k in range(min(first, outputs)):
        out.update(struct.pack("<f", reference.direct_sample(c, k)))
    k = first
    period = len(tile) // 4
    interior_end = min(outputs, ((c["frames"] - 32) * 22050 + c["sample_rate"] - 1) // c["sample_rate"])
    while k < interior_end:
        offset = k % period
        take = min(interior_end - k, period - offset)
        out.update(tile[offset * 4:(offset + take) * 4])
        k += take
    for k in range(max(first, interior_end), outputs):
        out.update(struct.pack("<f", reference.direct_sample(c, k)))
    return h.hexdigest(), out.hexdigest()


def worker(executable, name, output):
    p = protocol()
    if io.sha(ROOT / p["reference"]["path"]) != p["reference"]["sha256"]:
        raise ValueError("reference identity differs")
    fixture = ROOT / p["fixtures"]["path"]
    if io.sha(fixture) != p["fixtures"]["sha256"]:
        raise ValueError("fixture identity differs")
    f = strict_json(fixture.read_text())
    limits = p["limits"]
    resource.setrlimit(resource.RLIMIT_AS, (limits["address_space_bytes"],) * 2)
    resource.setrlimit(resource.RLIMIT_CPU, (limits["cpu_seconds"],) * 2)
    start = time.monotonic()
    out_path, err_path = output.with_suffix(".child.stdout"), output.with_suffix(".child.stderr")
    with out_path.open("x") as out, err_path.open("x") as err:
        child = subprocess.run([str(executable), "--case", name], stdout=out, stderr=err)
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    if child.returncode:
        raise ValueError("Rust child failed; raw logs retained")
    data = strict_json(out_path.read_text())
    assets = hashlib.sha256()
    for asset in sorted(p["assets"]):
        assets.update((ROOT / p["assets_root"] / asset).read_bytes())
    if (data["case"] != name or data["status"] != "COMPLETE"
            or data["protocol_sha256"] != EXPECTED_PROTOCOL
            or data["fixture_sha256"] != p["fixtures"]["sha256"]
            or data["asset_sha256"] != assets.hexdigest()):
        raise ValueError("executable/input identity mismatch")
    if (data["ring_bytes"] != 260 or data["table_bytes"] != 76960
            or data["input_buffer_bytes"] != 4096 or data["output_buffer_bytes"] != 1024):
        raise ValueError("owned storage geometry differs")
    specs = f["cases"] if name == "fixtures" else [next(c for c in f["full_cases"] if c["name"] == ("full48000-stereo" if name == "cancel" else name))]
    if len(specs) != len(data["observations"]):
        raise ValueError("incomplete observation denominator")
    for c, o in zip(specs, data["observations"]):
        if o["id"] != ("cancel" if name == "cancel" else c.get("id", c.get("name"))):
            raise ValueError("case order mismatch")
        if not 0 <= o["units"] <= p["algorithm"]["maximum_total_units"]:
            raise ValueError("work count exceeds capacity")
        if name == "cancel":
            if (o["status"] != "Cancelled" or o["final_certificate"]
                    or o["phase_at_cancel"] not in ["Fill", "Convolve", "Emit"]
                    or not 0 <= o["cancel_seconds"] * 1000 <= limits["host_cancel_request_to_return_ms"]
                    or not 0 <= o["output_count"] < c["expected"]["output_count"]
                    or not 0 <= o["source_frames"] <= c["frames"]):
                raise ValueError("asynchronous cancellation fence mismatch")
            ih, oh = prefix_hashes(c, o["source_frames"], o["output_count"])
            if (o["input_sha256"], o["output_sha256"]) != (ih, oh):
                raise ValueError("provisional cancelled prefix identity differs")
        else:
            independent = reference.short_oracle(c) if name == "fixtures" else reference.full_oracle(c)
            expected = c["expected"]
            for key in ["output_count", "output_sha256", "input_sha256", "status"]:
                if independent[key] != expected[key] or o[key] != expected[key]:
                    raise ValueError("synthetic scalar oracle mismatch: " + o["id"] + " " + key)
            count = expected["output_count"]
            if (o["source_frames"] != c["frames"] or o["macs"] != 65 * count
                    or o["units"] != c["frames"] + 67 * count or not o["final_certificate"]
                    or o["first_output_source_frames"] != (min(c["frames"], 33) if count else None)):
                raise ValueError("stream length, phase availability or work count mismatch")
            if name == "fixtures" and any((o[k] is None) != (expected[k] is None) or (o[k] is not None and struct.pack("<f", o[k]) != struct.pack("<f", expected[k])) for k in ["minimum", "maximum"]):
                raise ValueError("output extrema differ")
    if usage.ru_maxrss > limits["rss_kib"] or usage.ru_utime + usage.ru_stime > limits["cpu_seconds"]:
        raise ValueError("child high-water RSS/CPU cap exceeded")
    data.update(child_peak_rss_kib=usage.ru_maxrss, child_cpu_seconds=usage.ru_utime + usage.ru_stime,
                worker_wall_seconds=time.monotonic() - start,
                child_stdout_sha256=io.sha(out_path), child_stderr_sha256=io.sha(err_path),
                applied_rlimits={"AS": resource.getrlimit(resource.RLIMIT_AS), "CPU": resource.getrlimit(resource.RLIMIT_CPU)},
                validated_observations=len(specs))
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
    for entry in [p["document"], p["fixtures"], p["reference"]]:
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
    # Bind the actual installed compiler/build executables before measurement,
    # separately from loaded Python/ELF runtime files and the frozen CLI feature.
    rustup = (Path.home() / ".cargo/bin/rustup").resolve()
    io.bind(rustup, io.sha(rustup), bindings)
    toolchain = {"feature": p["feature"]["name"], "tools": {}}
    for name in ["rustc", "cargo"]:
        path = Path(subprocess.check_output([str(rustup), "which", name], text=True).strip()).resolve()
        io.bind(path, io.sha(path), bindings)
        toolchain["tools"][name] = {"path": str(path), "sha256": io.sha(path),
                                    "version_verbose": subprocess.check_output([str(path), "-vV"], text=True)}
    linked = subprocess.run(["ldd", str(args.executable.resolve())], capture_output=True, text=True, check=True)
    for token in linked.stdout.split():
        if token.startswith("/"):
            path = Path(token).resolve()
            io.bind(path, io.sha(path), bindings)
    subprocess.run([sys.executable, "-c", "pass"], check=True, capture_output=True, text=True, timeout=5)
    runtime_count = io.runtime_snapshot(bindings)
    args.output.mkdir(parents=True, exist_ok=False)
    final = {"format": "libapta-host-resampler-result-1", "protocol_sha256": EXPECTED_PROTOCOL,
             "source_revision": revision, "executable_sha256": io.sha(args.executable),
             "component_sha256": {n: io.sha(ROOT / n) for n in p["files_planned"]},
             "python": sys.version, "toolchain": toolchain, "parent_runtime_files": runtime_count, "model_corpus_or_device_executed": False}
    receipts = []
    try:
        print("HS-03 enforcement preflight", flush=True)
        final["enforcement"] = enforcement(args.output)
        io.write_new(args.output / "bindings.json", bindings)
        for case in p["measured_cases"]:
            name = case["name"]
            print("HS-03 start " + name, flush=True)
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
            print("HS-03 PASS " + name + " " + str(round(data["worker_wall_seconds"], 4)) + "s", flush=True)
        io.check_source(ROOT, revision)
        final.update(status="HS03_SYNTHETIC_HOST_PASS_ONLY", cases=receipts)
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
