#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""HS-04 fixed synthetic host supervisor and independent bounded pipe checker."""
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
PROTOCOL = ROOT / "docs/rust/PRODUCT-HOST-FRONTEND-PROTOCOL.json"
EXPECTED_PROTOCOL = "27cb6fe41fe4f94d22dbeac65c778feef0f2094c42aee267108d80ac17112b1f"
ERRATUM = ROOT / "docs/rust/PRODUCT-HOST-FRONTEND-COUNT-ERRATUM.md"
REFERENCE_ROOT = ROOT / "docs/rust/fixtures/product-host-frontend-hs04"
CASES = ["fixtures", "fullzero-44100", "fulldc2-44100", "fullpulse441-48000", "fullalternating2-48000", "cancel"]


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
        raise ValueError("HS04 registration changed")
    return strict_json(PROTOCOL.read_text())


def raw_values(entry):
    path = REFERENCE_ROOT / entry["path"]
    raw = path.read_bytes()
    if len(raw) != entry["bytes"] or hashlib.sha256(raw).hexdigest() != entry["sha256"]:
        raise ValueError("raw oracle identity differs")
    values = tuple(v[0] for v in struct.iter_unpack("<" + entry["format"], raw))
    if len(values) != entry["elements"] or not all(math.isfinite(v) for v in values):
        raise ValueError("raw oracle geometry/finite mismatch")
    return values


def rounded(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def class_index(case, row):
    representative = row if row in (0, 1, 29999) else 2 + row % 2
    return case["class_by_representative"][str(representative)]


def full_references(case):
    logs = [(raw_values(c["logs"]), raw_values(c["ideal_logs"])) for c in case["classes"]]
    # At most five classes, never a full feature tensor.
    def at(row):
        current, ideal = logs[class_index(case, row)]
        if row == 0:
            return current + (0.0,) * 144, ideal + (0.0,) * 144
        previous, previous_ideal = logs[class_index(case, row - 1)]
        diff = tuple(max(0.0, rounded(x - y)) for x, y in zip(current, previous))
        ideal_diff = tuple(max(0.0, x - y) for x, y in zip(ideal, previous_ideal))
        return current + diff, ideal + ideal_diff
    return at


def input_prefix_hash(case, count):
    h = hashlib.sha256(struct.pack("<IQQ", case["source_rate"], case["source_frames"], case["samples"]))
    if "input" in case:
        path = REFERENCE_ROOT / case["input"]["path"]
        h.update(path.read_bytes()[:4 * count])
    else:
        signal = case["signal"]
        period = {"zero": [0.0], "dc2": [2.0], "pulse441": [2.0] + [0.0] * 440,
                  "alternating2": [2.0, -2.0]}[signal]
        tile = b"".join(struct.pack("<f", x) for x in period)
        block = tile * 256
        blocks, remainder = divmod(count, len(period) * 256)
        for _ in range(blocks):
            h.update(block)
        copies, tail = divmod(remainder, len(period))
        h.update(tile * copies)
        h.update(tile[:tail * 4])
    return h.hexdigest()


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
        if not 0 <= count <= 28224 or self.count + count > self.max_bytes:
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

    def primitive(self, index, case):
        if self.read(5) != struct.pack("<BHH", 1, index, 1764):
            raise ValueError("primitive record order/shape mismatch")
        data = self.read(28224)
        start = time.monotonic()
        got = tuple(struct.iter_unpack("<dd", data))
        for key in ("analytic", "direct"):
            ref = raw_values(case[key])
            for k, (re, im) in enumerate(got):
                x, y = ref[2*k:2*k+2]
                if not math.isfinite(re) or not math.isfinite(im) or math.hypot(re-x, im-y) > 1e-4 + 2e-5*math.hypot(x, y):
                    self.receipt["failure_witness"] = {"primitive": index, "bin": k, "got": [repr(re), repr(im)], "reference": [x, y]}
                    raise SyntheticFailure("primitive numeric mismatch")
        self.check_seconds += time.monotonic() - start
        self.complete("primitive", index)

    def feature(self, index, row, references, payload=None):
        # For cancel, the caller already consumed the tag to detect its trailer.
        if payload is None and self.read(1) != b"\x02":
            raise ValueError("missing feature tag")
        if self.read(6) != struct.pack("<HI", index, row):
            raise ValueError("feature index/order mismatch")
        data = self.read(1152)
        start = time.monotonic()
        got = struct.unpack("<288f", data)
        for j, value in enumerate(got):
            if not math.isfinite(value) or not 0 <= value < 4 or (row == 0 and j >= 144 and data[j*4:j*4+4] != b"\0"*4):
                self.receipt["failure_witness"] = {"stream": index, "row": row, "scalar": j, "got": repr(value)}
                raise SyntheticFailure("feature finite/domain/first-difference mismatch")
            for ref in references:
                expected = ref[j]
                if abs(value-expected) > 2e-5 + 2e-5*abs(expected):
                    self.receipt["failure_witness"] = {"stream": index, "row": row, "scalar": j, "got": value, "reference": expected}
                    raise SyntheticFailure("feature numeric mismatch")
        self.check_seconds += time.monotonic() - start
        self.complete("feature", index, row)
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


def validate_metadata(data, specs, counts, hashes, name, p, fixtures):
    if (data["case"] != name or data["status"] != "COMPLETE"
            or data["protocol_sha256"] != EXPECTED_PROTOCOL or data["fixture_sha256"] != p["fixtures"]["sha256"]
            or not 40760 <= data["session_value_bytes"] <= 49152 or data["table_bytes"] != 50476
            or data["input_buffer_bytes"] != 2048 or data["output_buffer_bytes"] != 1152):
        raise ValueError("CLI identity/storage metadata mismatch")
    primitive_work = data["primitive_work"]
    if len(primitive_work) != (10 if name == "fixtures" else 0) or any(x["macs"] != 42336 or x["units"] <= 42336 for x in primitive_work):
        raise ValueError("primitive work denominator differs")
    if len(data["observations"]) != len(specs):
        raise ValueError("stream observation denominator differs")
    for c, observation, count, digest in zip(specs, data["observations"], counts, hashes):
        o = observation["stream"]
        if (o["id"] != c.get("id", c.get("name")) or o["produced_rows"] != count
                or o["output_sha256"] != digest or not 0 <= o["consumed_samples"] <= c["samples"]
                or not 0 <= o["units"] <= 4000000000 or not 0 <= o["macs"] <= 42336*c["rows"]):
            raise ValueError("stream identity/count/work differs")
        if o["input_sha256"] != input_prefix_hash(c, o["consumed_samples"]):
            raise ValueError("input prefix hash differs")
        if name == "cancel":
            if (o["status"] != "Cancelled" or o["final_certificate"] or count >= c["rows"]
                    or not 0 <= o["cancel_seconds"] <= .05
                    or o["phase_at_cancel"] not in p["API"]["phases"][:-1]):
                raise ValueError("cancel terminal/latency fence differs")
        elif (o["status"] != c["expected_status"] or not o["final_certificate"]
              or o["consumed_samples"] != c["samples"] or count != c["rows"] or o["macs"] != 42336*count):
            raise ValueError("complete stream geometry/status/work differs")
        for key in ["construction_seconds", "step_seconds", "max_step_seconds", "pipe_write_seconds", "driver_wall_seconds", "cancel_seconds"]:
            if not isinstance(o[key], (int, float)) or not math.isfinite(o[key]) or o[key] < 0:
                raise ValueError("invalid timing metadata")
    return len(specs) + len(primitive_work)


def worker(executable, name, output):
    p = protocol()
    if io.sha(ROOT / p["fixtures"]["path"]) != p["fixtures"]["sha256"]:
        raise ValueError("fixture identity differs")
    fixtures = strict_json((ROOT / p["fixtures"]["path"]).read_text())
    limits = p["limits"]
    resource.setrlimit(resource.RLIMIT_AS, (limits["address_space_bytes"],)*2)
    resource.setrlimit(resource.RLIMIT_CPU, (limits["cpu_seconds"],)*2)
    start = time.monotonic()
    specs = fixtures["short_cases"] if name == "fixtures" else [next(c for c in fixtures["full_cases"] if c["name"] == ("fullpulse441-48000" if name == "cancel" else name))]
    primitive_count = 10 if name == "fixtures" else 0
    cap = 8 + 28229*primitive_count + 1159*sum(c["rows"] for c in specs) + 5 + 16384
    counts, hashes = [], []
    err_path = output.with_suffix(".child.stderr")
    with err_path.open("xb") as err:
        child = subprocess.Popen([str(executable), "--case", name], stdout=subprocess.PIPE, stderr=err)
        checker = PipeChecker(child.stdout, cap)
        try:
            header = checker.read(8)
            checker.receipt["header_hex"] = header.hex()
            checker.receipt["header_ascii"] = header.decode("ascii")
            if header != b"HS04BIN1":
                raise ValueError("wrong pipe header")
            for i, c in enumerate(fixtures["primitive_cases"] if name == "fixtures" else []):
                checker.primitive(i, c)
            trailer_consumed = False
            for index, c in enumerate(specs):
                h = hashlib.sha256()
                count = 0
                if name == "fixtures":
                    narrow, ideal = raw_values(c["features"]), raw_values(c["ideal_features"])
                    reference = lambda r: (narrow[r*288:(r+1)*288], ideal[r*288:(r+1)*288])
                else:
                    reference = full_references(c)
                for row in range(c["rows"]):
                    if name == "cancel":
                        tag = checker.read(1)
                        if tag == b"\x03":
                            trailer_consumed = True
                            break
                        if tag != b"\x02":
                            raise ValueError("cancel prefix tag mismatch")
                    data = checker.feature(index, row, reference(row), payload=True if name == "cancel" else None)
                    h.update(data)
                    count += 1
                counts.append(count)
                hashes.append(h.hexdigest())
            data = checker.trailer(trailer_consumed)
            child.stdout.close()
            if child.wait() != 0:
                raise ValueError("Rust child failed")
            validated = validate_metadata(data, specs, counts, hashes, name, p, fixtures)
            own = resource.getrusage(resource.RUSAGE_SELF)
            usage = resource.getrusage(resource.RUSAGE_CHILDREN)
            combined = own.ru_utime + own.ru_stime + usage.ru_utime + usage.ru_stime
            if max(own.ru_maxrss, usage.ru_maxrss) > limits["rss_kib"] or combined > limits["cpu_seconds"]:
                raise ValueError("combined worker/child CPU or HWM RSS cap exceeded")
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
            checker.receipt["failure"] = str(exc)
            checker.receipt["failure_kind"] = "SYNTHETIC_FAIL" if isinstance(exc, SyntheticFailure) else "INVALID_OR_INCOMPLETE"
            if child.poll() is None:
                child.kill()
            child.wait()
            child.stdout.close()
            raise
        finally:
            checker.receipt.update(transport_sha256=checker.digest.hexdigest(), transport_bytes=checker.count,
                                   complete_records=checker.ordinal, validated_rows=counts,
                                   candidate_feature_sha256=hashes, child_returncode=child.returncode)
            io.write_new(output.with_suffix(".pipe-receipt.json"), checker.receipt)


def checker_preflight():
    """Exercise acceptance and refusal independently of Rust output."""
    header = struct.pack("<BHI", 2, 0, 0)
    zero = struct.pack("<288f", *([0.0]*288))
    refs = ((0.0,)*288,)*2
    good = header + zero
    p = PipeChecker(binary_io.BytesIO(good), 1159)
    assert p.feature(0, 0, refs) == zero
    cases = {
        "wrong_tag": b"\x01" + good[1:],
        "wrong_stream": struct.pack("<BHI", 2, 1, 0) + zero,
        "wrong_row": struct.pack("<BHI", 2, 0, 1) + zero,
        "truncated": good[:-1],
        "nonfinite": header + struct.pack("<f", float("nan")) + zero[4:],
        "numeric": header + struct.pack("<f", 1.0) + zero[4:],
        "first_negative_zero": header + zero[:576] + struct.pack("<f", -0.0) + zero[580:],
    }
    for name, raw in cases.items():
        try:
            PipeChecker(binary_io.BytesIO(raw), 1159).feature(0, 0, refs)
        except ValueError:
            pass
        else:
            raise ValueError("checker accepted " + name)
    for raw in [b'{"a":1,"a":2}', b'{"a":NaN}']:
        try:
            strict_json(raw)
        except ValueError:
            pass
        else:
            raise ValueError("strict JSON refusal failed")
    trailer = b"\x03" + struct.pack("<I", 2) + b"{}"
    assert PipeChecker(binary_io.BytesIO(trailer), 16389).trailer() == {}
    for raw in [trailer+b"x", trailer[:-1], b"\x03"+struct.pack("<I", 16385)]:
        try:
            PipeChecker(binary_io.BytesIO(raw), 16390).trailer()
        except ValueError:
            pass
        else:
            raise ValueError("trailer refusal failed")
    return {"accepted": 2, "refused": 12}


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
    if [c["name"] for c in p["measured_cases"]] != CASES:
        raise ValueError("registered six-case order differs")
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
    for name, entry in p["assets"].items():
        io.bind(ROOT / p["assets_root"] / name, entry["sha256"], bindings)
    for entry in p["fixtures"]["new_assets"].values():
        io.bind(ROOT / p["fixtures"]["new_assets_root"] / entry["path"], entry["sha256"], bindings)
    for name in p["files_planned"]:
        io.bind(ROOT / name, io.sha(ROOT / name), bindings)
    io.bind(PROTOCOL, EXPECTED_PROTOCOL, bindings)
    io.bind(ERRATUM, io.sha(ERRATUM), bindings)
    io.bind(args.executable, io.sha(args.executable), bindings)
    io.bind(Path(sys.executable).resolve(), io.sha(Path(sys.executable).resolve()), bindings)
    rustup = (Path.home() / ".cargo/bin/rustup").resolve()
    io.bind(rustup, io.sha(rustup), bindings)
    toolchain = {"feature": "experimental-compact-frontend", "tools": {}}
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
    final = {"format": "libapta-host-frontend-result-1", "protocol_sha256": EXPECTED_PROTOCOL,
             "erratum_sha256": io.sha(ERRATUM), "source_revision": revision,
             "executable_sha256": io.sha(args.executable), "component_sha256": {n: io.sha(ROOT / n) for n in p["files_planned"]},
             "python": sys.version, "toolchain": toolchain, "parent_runtime_files": runtime_count,
             "model_corpus_or_device_executed": False}
    receipts = []
    try:
        print("HS-04 enforcement preflight", flush=True)
        final["checker_preflight"] = checker_preflight()
        final["enforcement"] = enforcement(args.output)
        io.write_new(args.output / "bindings.json", bindings)
        for name in CASES:
            print("HS-04 start " + name, flush=True)
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
            print("HS-04 PASS " + name + " " + str(round(data["worker_wall_seconds"], 4)) + "s", flush=True)
        io.check_source(ROOT, revision)
        final.update(status="HS04_SYNTHETIC_HOST_PASS_ONLY", cases=receipts)
        if io.tree_bytes([args.output]) + io.json_bytes(final) > p["limits"]["output_bytes"]:
            raise ValueError("final aggregate output cap exceeded")
    except BaseException as exc:
        failures = [strict_json(path.read_text()) for path in args.output.glob("*.pipe-receipt.json")]
        verdict = "HS04_SYNTHETIC_FAIL" if any(r.get("failure_kind") == "SYNTHETIC_FAIL" for r in failures) else "INVALID_OR_INCOMPLETE"
        final.update(status=verdict, failure=str(exc), cases=receipts)
        io.write_new(args.output / "result.json", final)
        raise
    io.write_new(args.output / "result.json", final)
    print(json.dumps({"status": final["status"], "cases": len(receipts)}), flush=True)


if __name__ == "__main__":
    main()
