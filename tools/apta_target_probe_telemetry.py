#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Bounded offline RC04 telemetry ingestion. No serial, flashing or execution."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import sys

FORMAT = "M1_RESOURCE_PROBE_V1 "
MAX_LOG = 1048576
MAX_MANIFEST = 131072
MAX_LINE = 8192
MAX_OBSERVATIONS = 1024
MAX_OUTPUT = 2097152
U64 = (1 << 64) - 1
MEMORY_FIELDS = {"capacity_bytes", "used_bytes", "free_bytes", "minimum_free_bytes", "largest_free_block_bytes"}
CLASSES = {"internal", "psram", "dma"}
IDENTITY_FIELDS = {"run_id", "unit_id", "board", "image_id", "elf_sha256", "source_revision", "protocol_sha256", "chip_revision", "evidence_kind"}
PLAN_FIELDS = {"id", "case", "variant", "trial", "phase", "max_elapsed_ms"}
OBS_FIELDS = {"schema", "kind", "sequence", "identity", "observation_id", "case", "variant", "trial", "phase", "status", "ticks_start", "ticks_end", "elapsed_ticks", "clock_hz", "memory", "stack", "counters", "result_sha256"}
END_FIELDS = {"schema", "kind", "sequence", "identity", "observations"}


class Invalid(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise Invalid(message)


def keys(value, expected, context):
    require(type(value) is dict and set(value) == expected, context + ": missing/unknown fields")


def integer(value, low, high, context):
    require(type(value) is int and low <= value <= high, context + ": integer outside bound")
    return value


def token(value, context, limit=80):
    require(type(value) is str and re.fullmatch(r"[A-Za-z0-9_.:-]{1," + str(limit) + r"}", value) is not None, context + ": invalid token")


def hex_value(value, length, context):
    require(type(value) is str and re.fullmatch(r"[0-9a-f]{" + str(length) + r"}", value) is not None, context + ": invalid digest")


def strict_json(raw):
    def pairs(items):
        result = {}
        for k, v in items:
            require(k not in result, "duplicate JSON key")
            result[k] = v
        return result
    def constant(_):
        raise Invalid("nonfinite JSON number")
    def bounded_int(value):
        require(len(value) <= 21, "JSON integer too long")
        return int(value)
    def reject_float(_):
        raise Invalid("floating JSON numbers are not part of telemetry V1")
    try:
        return json.loads(raw.decode("utf-8", errors="strict"), object_pairs_hook=pairs,
                          parse_constant=constant, parse_int=bounded_int, parse_float=reject_float)
    except (ValueError, UnicodeError, RecursionError) as exc:
        raise Invalid(str(exc)) from exc


def validate_manifest(manifest):
    keys(manifest, {"schema", "identity", "clock_hz", "counter_bits", "observations"}, "manifest")
    require(type(manifest["schema"]) is int and manifest["schema"] == 1, "manifest schema")
    ident = manifest["identity"]
    keys(ident, IDENTITY_FIELDS, "manifest identity")
    for k in ["run_id", "unit_id", "image_id"]:
        token(ident[k], k)
    require(ident["board"] == "JC1060P470C_I_W_Y", "wrong target board")
    for k, n in [("elf_sha256", 64), ("source_revision", 40), ("protocol_sha256", 64)]:
        hex_value(ident[k], n, k)
    integer(ident["chip_revision"], 302, 65535, "chip revision")
    require(ident["evidence_kind"] in ("synthetic", "device_log"), "evidence kind")
    integer(manifest["clock_hz"], 1000, 1000000000, "clock Hz")
    require(type(manifest["counter_bits"]) is int and manifest["counter_bits"] in (32, 64), "counter bits")
    plans = manifest["observations"]
    require(type(plans) is list and 1 <= len(plans) <= MAX_OBSERVATIONS, "observation plan bound")
    ids = set()
    for plan in plans:
        keys(plan, PLAN_FIELDS, "observation plan")
        for k in ["id", "variant", "phase"]:
            token(plan[k], "plan " + k)
        require(plan["id"] not in ids, "duplicate planned observation")
        ids.add(plan["id"])
        require(plan["case"] in [f"TP-{i:02}" for i in range(7)], "unknown probe case")
        integer(plan["trial"], 0, 1024, "trial")
        integer(plan["max_elapsed_ms"], 1, 300000, "elapsed bound")
        require(plan["max_elapsed_ms"] * manifest["clock_hz"] < (1 << manifest["counter_bits"]) * 1000,
                "planned interval can span a whole counter period")
    return manifest


def metric(value, context):
    if value is None:
        return 1
    integer(value, 0, U64, context)
    return 0


def memory_metrics(value):
    keys(value, CLASSES, "memory classes")
    unknown = 0
    for name, mem in value.items():
        keys(mem, MEMORY_FIELDS, "memory " + name)
        unknown += sum(metric(v, name + "." + k) for k, v in mem.items())
        capacity, used, free = (mem[k] for k in ["capacity_bytes", "used_bytes", "free_bytes"])
        if capacity is not None:
            require(all(v is None or v <= capacity for v in mem.values()), "memory metric exceeds class capacity")
        if all(v is not None for v in (capacity, used, free)):
            require(used + free == capacity, "used/free/capacity mismatch")
        if free is not None:
            require(all(mem[k] is None or mem[k] <= free for k in ["minimum_free_bytes", "largest_free_block_bytes"]), "minimum/largest exceeds free")
    return unknown


def validate_observation(record, plan, manifest):
    keys(record, OBS_FIELDS, "observation")
    for k in ["case", "variant", "trial", "phase"]:
        # bool compares equal to int; type equality is intentional here.
        require(type(record[k]) is type(plan[k]) and record[k] == plan[k], "observation plan mismatch: " + k)
    require(record["observation_id"] == plan["id"], "observation ID/order mismatch")
    require(record["status"] in ("MEASURED", "NOT_SUPPORTED", "FAILED", "CANCELLED"), "unknown observation status")
    require(type(record["clock_hz"]) is int and record["clock_hz"] == manifest["clock_hz"], "clock mismatch")
    modulus = 1 << manifest["counter_bits"]
    for k in ["ticks_start", "ticks_end", "elapsed_ticks"]:
        integer(record[k], 0, modulus - 1, k)
    require((record["ticks_start"] + record["elapsed_ticks"]) % modulus == record["ticks_end"], "tick interval mismatch")
    require(record["elapsed_ticks"] * 1000 <= plan["max_elapsed_ms"] * manifest["clock_hz"], "elapsed cap exceeded")
    unknown = memory_metrics(record["memory"])
    stack = record["stack"]
    keys(stack, {"capacity_bytes", "high_water_used_bytes"}, "stack")
    unknown += sum(metric(v, "stack." + k) for k, v in stack.items())
    if all(v is not None for v in stack.values()):
        require(stack["high_water_used_bytes"] <= stack["capacity_bytes"], "stack high water exceeds capacity")
    counters = record["counters"]
    keys(counters, {"audio_underruns", "usb_errors"}, "counters")
    unknown += sum(metric(v, k) for k, v in counters.items())
    if record["result_sha256"] is not None:
        hex_value(record["result_sha256"], 64, "result digest")
    return dict(id=plan["id"], sequence=record["sequence"], status=record["status"],
                elapsed_ticks=record["elapsed_ticks"], unknown_metrics=unknown,
                result_sha256=record["result_sha256"],
                nonzero_error_counters=[k for k, v in counters.items() if v is not None and v > 0])


def analyze(raw, manifest):
    """Validate a bounded dedicated resource log; retain only bounded summaries.

    Digest consistency is not proof of the flashed image, trustworthy device
    readings, numerical correctness or product acceptance.
    """
    validate_manifest(manifest)
    result = dict(format="libapta-target-probe-ingest-1", status="INCOMPLETE",
                  evidence_kind=manifest["identity"]["evidence_kind"], identity=manifest["identity"],
                  qualification="NOT_QUALIFIED", experiment_verdict="NOT_EVALUATED",
                  physical_measurement_verified=False, programmed_image_verified=False,
                  clock_hz=manifest["clock_hz"], expected_observations=len(manifest["observations"]),
                  completed_observations=0, terminal_seen=False, observations=[], unknown_metrics=0,
                  errors=[], last_complete_sequence=None, input_bytes=len(raw),
                  raw_log_sha256=hashlib.sha256(raw).hexdigest())
    if len(raw) > MAX_LOG:
        result.update(status="INVALID", errors=[dict(line=None, reason="log byte cap exceeded")])
        return result
    ended = False
    ordinal = 0
    stream = io.BytesIO(raw)
    while True:
        line = stream.readline(MAX_LINE + 1)
        if not line:
            break
        ordinal += 1
        try:
            require(not ended, "data after terminal record")
            require(len(line) <= MAX_LINE, "record byte cap exceeded")
            require(line.endswith(b"\n"), "partial final line")
            # Dedicated protocol, not a permissive boot-log noise filter.
            require(line.startswith(FORMAT.encode("ascii")), "unknown record prefix")
            data = strict_json(line[len(FORMAT):])
            require(type(data) is dict, "record must be an object")
            require(type(data.get("schema")) is int and data["schema"] == 1, "record schema")
            integer(data.get("sequence"), 0, MAX_OBSERVATIONS, "sequence")
            require(data["sequence"] == len(result["observations"]), "duplicate/gapped/out-of-order sequence")
            ident = data.get("identity")
            keys(ident, IDENTITY_FIELDS, "record identity")
            require(all(type(ident[k]) is type(v) and ident[k] == v for k, v in manifest["identity"].items()), "unit/image/source/protocol identity mismatch")
            if data.get("kind") == "end":
                keys(data, END_FIELDS, "terminal record")
                integer(data["observations"], 0, MAX_OBSERVATIONS, "terminal count")
                require(data["observations"] == len(result["observations"]) == len(manifest["observations"]), "missing observations at terminal")
                ended = True
                result["terminal_seen"] = True
            else:
                require(data.get("kind") == "observation", "unknown record kind")
                require(len(result["observations"]) < len(manifest["observations"]), "unplanned observation")
                summary = validate_observation(data, manifest["observations"][len(result["observations"])], manifest)
                result["observations"].append(summary)
                result["unknown_metrics"] += summary["unknown_metrics"]
            result["last_complete_sequence"] = data["sequence"]
        except Invalid as exc:
            partial = not ended and line.startswith(FORMAT.encode("ascii")) and not line.endswith(b"\n") and len(line) <= MAX_LINE
            result.update(status="INCOMPLETE" if partial else "INVALID", errors=[dict(line=ordinal, reason=str(exc))])
            break
    result["completed_observations"] = len(result["observations"])
    if not result["errors"]:
        if ended:
            result["status"] = "VALID_WITH_UNKNOWN_METRICS" if result["unknown_metrics"] else "VALID_COMPLETE"
        else:
            result["errors"] = [dict(line=ordinal + 1, reason="missing terminal record")]
    result["failed_or_unsupported_observations"] = [o["id"] for o in result["observations"] if o["status"] != "MEASURED"]
    result["observations_with_error_counters"] = [o["id"] for o in result["observations"] if o["nonzero_error_counters"]]
    return result


def json_bytes(value):
    return (json.dumps(value, sort_keys=True, indent=2, allow_nan=False) + "\n").encode("utf-8")


def ingest(manifest_path, log_path, output):
    require(not output.exists(), "output must be a new directory")
    # Read at most cap+1 so oversized/partial input cannot allocate unboundedly.
    with manifest_path.open("rb") as f:
        manifest_raw = f.read(MAX_MANIFEST + 1)
    with log_path.open("rb") as f:
        raw = f.read(MAX_LOG + 1)
    try:
        require(len(manifest_raw) <= MAX_MANIFEST, "manifest byte cap exceeded")
        manifest = validate_manifest(strict_json(manifest_raw))
        result = analyze(raw, manifest)
    except Invalid as exc:
        result = dict(format="libapta-target-probe-ingest-1", status="INVALID",
                      errors=[dict(line=None, reason="invalid manifest: " + str(exc))],
                      qualification="NOT_QUALIFIED", experiment_verdict="NOT_EVALUATED",
                      physical_measurement_verified=False, programmed_image_verified=False)
    result.update(manifest_sha256=hashlib.sha256(manifest_raw).hexdigest(),
                  retained_raw_log_sha256=hashlib.sha256(raw).hexdigest(),
                  retained_manifest_bytes=len(manifest_raw), retained_raw_log_bytes=len(raw),
                  manifest_truncated_at_cap=len(manifest_raw) > MAX_MANIFEST,
                  log_truncated_at_cap=len(raw) > MAX_LOG,
                  truncation_scope="Oversized inputs retain cap+1 prefix; hashes describe retained bytes, not unread suffix")
    receipt = json_bytes(result)
    require(len(manifest_raw) + len(raw) + len(receipt) <= MAX_OUTPUT, "retained output byte cap exceeded")
    # An exclusive directory also prevents outputs overwriting any source file.
    output.mkdir(parents=True, exist_ok=False)
    for name, data in [("manifest.json", manifest_raw), ("raw.log", raw), ("result.json", receipt)]:
        with (output / name).open("xb") as f:
            f.write(data)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = ingest(args.manifest, args.log, args.output)
        print(json.dumps({"status": result["status"], "qualification": "NOT_QUALIFIED"}))
        return 0 if result["status"] in ("VALID_COMPLETE", "VALID_WITH_UNKNOWN_METRICS") else 2
    except (Invalid, OSError) as exc:
        parser.exit(1, "ERROR: " + str(exc) + "\n")


if __name__ == "__main__":
    sys.exit(main())
