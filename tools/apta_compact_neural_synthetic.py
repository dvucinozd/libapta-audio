#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""HS-05 artificial recurrent host supervisor and independent bounded pipe checker."""
import argparse
import hashlib
import io as binary_io
import json
import math
from pathlib import Path
import resource
import struct
import subprocess
import sys
import time
import apta_dense_runner_support as io

ROOT = Path(__file__).resolve().parents[1]
PROTOCOL = ROOT / "docs/rust/PRODUCT-HOST-NEURAL-PROTOCOL.json"
EXPECTED_PROTOCOL = "8942bed1390a9bba5db3779c73a508b8933b1ee3aa242e2e0870d65c8403c058"
REFERENCE_ROOT = ROOT / "docs/rust/fixtures/product-host-neural-hs05"
CASES = ["fixtures", "long-zero", "long-carry", "long-reset", "cancel"]
sys.path.insert(0, str(REFERENCE_ROOT))
import scalar_reference as reference


class SyntheticFailure(ValueError):
    """Complete numeric/semantic disagreement, distinct from run integrity."""


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
        raise ValueError("HS05 registration changed")
    return strict_json(PROTOCOL.read_text())


class PipeChecker:
    """No read-all/spooling. Keep only one bounded record and a partial receipt."""
    def __init__(self, stream, max_bytes):
        self.stream = stream
        self.max_bytes = max_bytes
        self.digest = hashlib.sha256()
        self.count = 0
        self.ordinal = 0
        self.wait_seconds = 0.0
        self.check_seconds = 0.0
        self.receipt = {"status": "INCOMPLETE", "last_complete_record": None}

    def read(self, count):
        if not 0 <= count <= 16384 or self.count + count > self.max_bytes:
            raise ValueError("bounded transport size exceeded")
        raw = bytearray()
        start = time.monotonic()
        while len(raw) < count:
            block = self.stream.read(count - len(raw))
            if not block:
                raise ValueError("truncated pipe record")
            self.digest.update(block)
            self.count += len(block)
            raw.extend(block)
        self.wait_seconds += time.monotonic() - start
        return bytes(raw)

    def complete(self, kind, index, row=None):
        self.ordinal += 1
        self.receipt["last_complete_record"] = {"ordinal": self.ordinal, "kind": kind, "index": index, "row": row}

    def values(self, index, row, data, expected, frozen=None, exact=()):
        start = time.monotonic()
        got = struct.unpack("<"+"f"*(len(data)//4), data)
        if len(got) != len(expected):
            raise ValueError("scalar denominator mismatch")
        for j, (x, y) in enumerate(zip(got, expected)):
            if not math.isfinite(x) or abs(x-y) > 2e-6+2e-5*abs(y):
                self.receipt["failure_witness"] = {"index": index, "row": row, "scalar": j, "got": repr(x), "reference": y}
                raise SyntheticFailure("scalar numeric mismatch")
            if frozen is not None:
                z = reference.from_bits(frozen[j])
                if abs(x-z) > 2e-6+2e-5*abs(z) or (j in exact and struct.pack("<f", x) != struct.pack("<I", frozen[j])):
                    raise SyntheticFailure("frozen exact/numeric primitive mismatch")
        self.check_seconds += time.monotonic()-start
        return got

    def probe(self, index, case, parameters):
        frozen = list(case["expected_bits"])
        kind = case["kind"]
        inp = case["input_bits"]
        x = [reference.from_bits(v) for v in inp] if isinstance(inp, list) else reference.from_bits(inp)
        exact = []
        if kind == "linear":
            expected = reference.dense(x, [reference.from_bits(v) for v in case["weight_bits"]], [reference.from_bits(v) for v in case["bias_bits"]])
            exact = [0, 1]
        elif kind == "activation":
            expected = case["ideal_f64"]
        elif kind == "softmax":
            expected, inter = reference.softmax(x)
            expected += inter["delta"]+inter["exp"]+inter["sum"]
            frozen += case["intermediate_bits"]["delta"]+case["intermediate_bits"]["exp"]+case["intermediate_bits"]["sum"]
            exact = list(range(3, 6))
            if len(set(inp)) == 1:
                exact = list(range(6))
        else:
            h, c, inter = reference.cell(parameters[case["profile"]], case["layer"], x,
                                         [reference.from_bits(v) for v in case["h_bits"]], [reference.from_bits(v) for v in case["c_bits"]])
            expected = h+c
            for key in ["ih", "hh", "z", "gates"]:
                expected += inter[key]
                frozen += case["intermediate_bits"][key]
            exact = list(range(4, 28))
        if self.read(5) != struct.pack("<BHH", 1, index, len(expected)):
            raise ValueError("probe tag/index/shape mismatch")
        got = self.values(index, None, self.read(4*len(expected)), expected, frozen, exact)
        if kind == "softmax" and winner(got[:3]) != case["argmax"]:
            raise SyntheticFailure("probe argmax mismatch")
        self.complete("probe", index)

    def row(self, index, row, expected, tag_consumed=False):
        if not tag_consumed and self.read(1) != b"\x02":
            raise ValueError("missing row tag")
        if self.read(6) != struct.pack("<HI", index, row):
            raise ValueError("row identity/order mismatch")
        data = self.read(44)
        got = self.values(index, row, data, expected)
        if (any(not 0 <= x <= 1 for x in got[:3]) or abs(sum(got[:3])-1) > 2e-6
                or any(abs(x) > 1 for x in got[3:7]) or any(abs(x) > row+1 for x in got[7:])
                or winner(got[:3]) != winner(expected[:3])):
            raise SyntheticFailure("state/probability/argmax invariant mismatch")
        self.complete("row", index, row)
        return data

    def trailer(self, consumed=False):
        if not consumed and self.read(1) != b"\x03":
            raise ValueError("missing trailer tag")
        length = struct.unpack("<I", self.read(4))[0]
        if length > 16384:
            raise ValueError("trailer cap exceeded")
        data = self.read(length)
        self.receipt["trailer_hex"] = data.hex()
        self.receipt["trailer_utf8"] = data.decode("utf-8")
        extra = self.stream.read(1)
        if extra:
            self.digest.update(extra)
            self.count += 1
            self.receipt["extra_byte_hex"] = extra.hex()
            raise ValueError("extra bytes after trailer")
        self.complete("trailer", 0)
        return strict_json(data)



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


def winner(p):
    return max(range(3), key=lambda j: p[j])


def parameters_and_cases(p):
    if io.sha(ROOT/p["fixtures"]["path"]) != p["fixtures"]["sha256"] or io.sha(ROOT/p["reference"]["path"]) != p["reference"]["sha256"]:
        raise ValueError("oracle identity mismatch")
    f = strict_json((ROOT/p["fixtures"]["path"]).read_text())
    parameters = {}
    for name, entry in f["profiles"].items():
        raw = (REFERENCE_ROOT/entry["path"]).read_bytes()
        if len(raw) != 452 or hashlib.sha256(raw).hexdigest() != entry["sha256"]:
            raise ValueError("parameter identity mismatch")
        parameters[name] = struct.unpack("<113f", raw)
    return f, parameters


def input_hash(case, accepted):
    h = hashlib.sha256(struct.pack("<Q", case["rows"]))
    for i in range(accepted):
        h.update(struct.pack("<3f", *reference.input_row(case["signal"], i)))
    return h.hexdigest()


def metadata_check(data, name, p, f, specs, counts, hashes, last_states):
    if (data["case"] != name or data["status"] != "COMPLETE" or data["protocol_sha256"] != EXPECTED_PROTOCOL
            or data["fixture_sha256"] != p["fixtures"]["sha256"] or not 256 <= data["session_value_bytes"] <= 4096
            or not 144 <= data["probe_value_bytes"] <= 4096 or data["profile_bytes"] != 1808
            or data["input_buffer_bytes"] != 768 or data["output_buffer_bytes"] != 12 or data["snapshot_bytes"] != 32):
        raise ValueError("component identity/storage mismatch")
    probes = f["primitive_cases"] if name == "fixtures" else []
    if len(data["probes"]) != len(probes) or len(data["observations"]) != len(specs):
        raise ValueError("observation denominator mismatch")
    for i, (obs, case) in enumerate(zip(data["probes"], probes)):
        macs = {"linear": 6, "activation": 0, "softmax": 0, "cell": 32}[case["kind"]]
        if obs["index"] != i or obs["macs"] != macs or not 1 <= obs["units"] <= 256:
            raise SyntheticFailure("probe work mismatch")
    for case, obs, count, digest, last in zip(specs, data["observations"], counts, hashes, last_states):
        o = obs["stream"]
        if (o["id"] != case.get("id", case.get("name")) or o["committed_rows"] != count or o["trace_sha256"] != digest
                or o["total_units"] != o["step_units"]+o["reset_units"] or o["reset_count"] != o["reset_units"]
                or not 0 <= o["total_units"] <= 6000000 or o["final_state_bits"] != last):
            raise SyntheticFailure("stream counters/state/trace mismatch")
        if not count <= o["accepted_rows"] <= min(count+1, case["rows"]) or o["input_sha256"] != input_hash(case, o["accepted_rows"]):
            raise ValueError("accepted input prefix mismatch")
        if name == "cancel":
            if (o["status"] != "Cancelled" or o["final_certificate"] or count >= 30000 or not 0 <= o["cancel_seconds"] <= .05
                    or o["phase_at_cancel"] not in p["API"]["phases"][:-1]
                    or not 172*count <= o["step_units"] <= 172*count+171 or not 76*count <= o["macs"] <= 76*(count+1)
                    or o["reset_count"] != 0):
                raise SyntheticFailure("cancel work/status/latency mismatch")
        elif (o["status"] != case["status"] or not o["final_certificate"] or o["accepted_rows"] != case["rows"] or count != case["rows"]
              or o["step_units"] != 172*count or o["macs"] != 76*count or o["reset_count"] != len(case["reset_before_rows"])):
            raise SyntheticFailure("complete row/state/reset/work mismatch")
        for key in ["construction_seconds", "step_seconds", "max_step_seconds", "reset_seconds", "pipe_write_seconds", "driver_wall_seconds", "cancel_seconds"]:
            if not isinstance(o[key], (int, float)) or not math.isfinite(o[key]) or o[key] < 0:
                raise ValueError("invalid timing metadata")
    return len(probes)+len(specs)


def worker(executable, name, output):
    p = protocol()
    f, parameters = parameters_and_cases(p)
    limits = p["limits"]
    resource.setrlimit(resource.RLIMIT_AS, (limits["address_space_bytes"],)*2)
    resource.setrlimit(resource.RLIMIT_CPU, (limits["cpu_seconds"],)*2)
    start = time.monotonic()
    specs = f["short_cases"] if name == "fixtures" else [next(c for c in f["full_cases"] if c["name"] == ("long-carry" if name == "cancel" else name))]
    cap = 8+149*(34 if name == "fixtures" else 0)+51*sum(c["rows"] for c in specs)+5+16384
    counts, hashes, last_states = [], [], []
    err_path = output.with_suffix(".child.stderr")
    with err_path.open("xb") as err:
        child = subprocess.Popen([str(executable), "--case", name], stdout=subprocess.PIPE, stderr=err)
        checker = PipeChecker(child.stdout, cap)
        try:
            header = checker.read(8)
            checker.receipt["header_hex"] = header.hex()
            if header != b"HS05BIN1":
                raise ValueError("wrong header")
            for i, c in enumerate(f["primitive_cases"] if name == "fixtures" else []):
                checker.probe(i, c, parameters)
            trailer_consumed = False
            for index, case in enumerate(specs):
                oracle = iter(reference.reference_stream(case, parameters[case["profile"]]))
                h, ref_hash = hashlib.sha256(), hashlib.sha256()
                count, last = 0, [0]*8
                for r in range(case["rows"]):
                    if name == "cancel":
                        tag = checker.read(1)
                        if tag == b"\x03":
                            trailer_consumed = True
                            break
                        if tag != b"\x02":
                            raise ValueError("cancel prefix tag mismatch")
                    _, _, expected = next(oracle)
                    ref_raw = struct.pack("<11f", *expected)
                    if name == "fixtures":
                        entry = case["trace"]
                        # These short arrays total3960B; never a full sequence cache.
                        with (REFERENCE_ROOT/entry["path"]).open("rb") as source:
                            source.seek(44*r)
                            if source.read(44) != ref_raw:
                                raise ValueError("frozen short reference differs")
                    raw = checker.row(index, r, expected, name == "cancel")
                    h.update(raw); ref_hash.update(ref_raw)
                    last = list(struct.unpack("<8I", raw[12:]))
                    count += 1
                if name != "cancel" and ref_hash.hexdigest() != case["expected"]["trace_sha256"]:
                    raise ValueError("complete independent reference digest differs")
                counts.append(count); hashes.append(h.hexdigest()); last_states.append(last)
            data = checker.trailer(trailer_consumed)
            child.stdout.close()
            if child.wait() != 0:
                raise ValueError("Rust child failed")
            validated = metadata_check(data, name, p, f, specs, counts, hashes, last_states)
            own, usage = resource.getrusage(resource.RUSAGE_SELF), resource.getrusage(resource.RUSAGE_CHILDREN)
            combined = own.ru_utime+own.ru_stime+usage.ru_utime+usage.ru_stime
            if max(own.ru_maxrss, usage.ru_maxrss) > limits["rss_kib"] or combined > limits["cpu_seconds"]:
                raise ValueError("combined CPU or HWM RSS exceeded")
            runtime = {}
            runtime_count = io.runtime_snapshot(runtime)
            io.write_new(output.with_suffix(".runtime.json"), runtime)
            data.update(validated_observations=validated, worker_wall_seconds=time.monotonic()-start,
                        combined_cpu_seconds=combined, worker_peak_rss_kib=own.ru_maxrss,
                        child_peak_rss_kib=usage.ru_maxrss, child_cpu_seconds=usage.ru_utime+usage.ru_stime,
                        pipe_wait_seconds=checker.wait_seconds, checker_seconds=checker.check_seconds,
                        worker_runtime_files=runtime_count, child_stderr_sha256=io.sha(err_path),
                        applied_rlimits={"AS": resource.getrlimit(resource.RLIMIT_AS), "CPU": resource.getrlimit(resource.RLIMIT_CPU)})
            checker.receipt["status"] = "PASS"
            io.write_new(output, data)
        except BaseException as exc:
            checker.receipt.update(failure=str(exc), failure_kind="SYNTHETIC_FAIL" if isinstance(exc, SyntheticFailure) else "INVALID_OR_INCOMPLETE")
            if child.poll() is None:
                child.kill()
            child.wait(); child.stdout.close()
            raise
        finally:
            checker.receipt.update(transport_sha256=checker.digest.hexdigest(), transport_bytes=checker.count,
                                   complete_records=checker.ordinal, validated_rows=counts,
                                   candidate_trace_sha256=hashes, child_returncode=child.returncode)
            io.write_new(output.with_suffix(".pipe-receipt.json"), checker.receipt)



def checker_preflight():
    values = [reference.f32(1/3)]*3+[0.]*8
    raw = b"\x02"+struct.pack("<HI11f", 0, 0, *values)
    checker = PipeChecker(binary_io.BytesIO(raw), 51)
    assert checker.row(0, 0, values) == raw[7:]
    bad = [b"\x01"+raw[1:], raw[:1]+struct.pack("<H", 1)+raw[3:],
           raw[:3]+struct.pack("<I", 1)+raw[7:], raw[:-1],
           raw[:7]+struct.pack("<f", float("nan"))+raw[11:],
           raw[:7]+struct.pack("<f", 0.)+raw[11:], raw[:19]+struct.pack("<f", 2.)+raw[23:]]
    for data in bad:
        try:
            PipeChecker(binary_io.BytesIO(data), 51).row(0, 0, values)
        except ValueError:
            pass
        else:
            raise ValueError("checker accepted malformed numeric/row transport")
    f, weights = parameters_and_cases(protocol())
    probe = struct.pack("<BHHff", 1, 0, 2, 0., 0.)
    PipeChecker(binary_io.BytesIO(probe), 13).probe(0, f["primitive_cases"][0], weights)
    try:
        changed = probe[:5]+struct.pack("<I", 1)+probe[9:]
        PipeChecker(binary_io.BytesIO(changed), 13).probe(0, f["primitive_cases"][0], weights)
    except SyntheticFailure:
        pass
    else:
        raise ValueError("exact linear bit mismatch was accepted")
    for data in [b'{"a":1,"a":2}', b'{"a":NaN}']:
        try:
            strict_json(data)
        except ValueError:
            pass
        else:
            raise ValueError("strict JSON failure")
    trailer = b"\x03"+struct.pack("<I", 2)+b"{}"
    assert PipeChecker(binary_io.BytesIO(trailer), 16389).trailer() == {}
    for data in [trailer+b"x", trailer[:-1], b"\x03"+struct.pack("<I", 16385)]:
        try:
            PipeChecker(binary_io.BytesIO(data), 16390).trailer()
        except ValueError:
            pass
        else:
            raise ValueError("trailer refusal failed")
    return {"accepted": 3, "refused": 13}


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
    if [c["name"] for c in p["measured_cases"]] != CASES:
        raise ValueError("registered five-case order differs")
    revision = io.git(ROOT, "rev-parse", "HEAD")
    io.check_source(ROOT, revision)
    io.external(args.output, ROOT)
    io.external(args.executable, ROOT)
    io.disjoint_output(args.output, [args.executable])
    bindings = {}
    for entry in [p["document"], p["fixtures"], p["reference"], p["registration_verifier"]]:
        io.bind(ROOT / entry["path"], entry["sha256"], bindings)
    for name, digest in p["authority_sha256"].items():
        io.bind(ROOT / name, digest, bindings)
    for entry in p["assets"].values():
        io.bind(ROOT / p["asset_root"] / entry["path"], entry["sha256"], bindings)
    for field in ["frozen_frontend_assets", "frozen_CS02_assets"]:
        group = p[field]
        for name, entry in group["assets"].items():
            io.bind(ROOT / group["root"] / entry.get("path", name), entry["sha256"], bindings)
    for name in p["files_planned"]:
        io.bind(ROOT / name, io.sha(ROOT / name), bindings)
    io.bind(PROTOCOL, EXPECTED_PROTOCOL, bindings)
    io.bind(args.executable, io.sha(args.executable), bindings)
    io.bind(Path(sys.executable).resolve(), io.sha(Path(sys.executable).resolve()), bindings)
    rustup = (Path.home() / ".cargo/bin/rustup").resolve()
    io.bind(rustup, io.sha(rustup), bindings)
    toolchain = {"feature": "experimental-compact-neural", "tools": {}}
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
    final = {"format": "libapta-host-neural-result-1", "protocol_sha256": EXPECTED_PROTOCOL,
             "source_revision": revision,
             "executable_sha256": io.sha(args.executable), "component_sha256": {n: io.sha(ROOT / n) for n in p["files_planned"]},
             "python": sys.version, "toolchain": toolchain, "parent_runtime_files": runtime_count,
             "model_corpus_or_device_executed": False}
    receipts = []
    try:
        print("HS-05 enforcement preflight", flush=True)
        final["checker_preflight"] = checker_preflight()
        final["enforcement"] = enforcement(args.output)
        io.write_new(args.output / "bindings.json", bindings)
        for name in CASES:
            print("HS-05 start " + name, flush=True)
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
            runtime = strict_json(path.with_suffix(".runtime.json").read_text())
            if any(bindings.get(n) != h for n, h in runtime.items()):
                raise ValueError("worker runtime was not prebound")
            io.recheck(runtime)
            print("HS-05 PASS " + name + " " + str(round(data["worker_wall_seconds"], 4)) + "s", flush=True)
        io.check_source(ROOT, revision)
        final.update(status="HS05_SYNTHETIC_HOST_PASS_ONLY", cases=receipts)
        if io.tree_bytes([args.output]) + io.json_bytes(final) > p["limits"]["output_bytes"]:
            raise ValueError("final aggregate output cap exceeded")
    except BaseException as exc:
        failures = [strict_json(path.read_text()) for path in args.output.glob("*.pipe-receipt.json")]
        verdict = "HS05_SYNTHETIC_FAIL" if any(r.get("failure_kind") == "SYNTHETIC_FAIL" for r in failures) else "INVALID_OR_INCOMPLETE"
        final.update(status=verdict, failure=str(exc), cases=receipts)
        io.write_new(args.output / "result.json", final)
        raise
    io.write_new(args.output / "result.json", final)
    print(json.dumps({"status": final["status"], "cases": len(receipts)}), flush=True)


if __name__ == "__main__":
    main()
