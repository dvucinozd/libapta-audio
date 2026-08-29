#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Validate physical ESP32-P4 evidence for the APTA 1.1 release gate."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

SCHEMA = "apta-1.1-esp32-p4-hardware-evidence-1"
REPORT_SCHEMA = "apta-1.1-esp32-p4-hardware-report-1"
MIN_DURATION_SECONDS = 1800
EXPECTED_INPUT_FRAMES = 48_000 * MIN_DURATION_SECONDS
EXPECTED_INPUT_BYTES = EXPECTED_INPUT_FRAMES * 2
MIN_WORKSPACE_BYTES = 941216
MIN_RESULT_POOL_BYTES = 537104
MAX_OVERVIEW_COLUMNS = 4096
MAX_RESIDENT_BEAT_RECORDS = 9216
REQUIRED_FEATURES = {
    "waveform_overview",
    "waveform_detail",
    "waveform_3band",
    "bpm",
    "local_beatgrid",
    "global_beatgrid",
    "dynamic_tempo",
    "confidence",
    "grid_locking",
    "meter_downbeat",
    "musical_key",
    "calibrated_quality",
}


class EvidenceError(ValueError):
    pass


def load(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise EvidenceError(f"cannot read evidence: {exc}") from exc
    if not isinstance(value, dict):
        raise EvidenceError("top-level evidence must be an object")
    return value


def _positive_int(value: Any, name: str, minimum: int = 1) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value < minimum:
        raise EvidenceError(f"{name} must be an integer >= {minimum}")
    return value


def evaluate(value: dict[str, Any], expected_source_revision: str | None = None) -> dict[str, Any]:
    failures: list[str] = []

    def fail(message: str) -> None:
        failures.append(message)

    if value.get("schema") != SCHEMA:
        fail("unexpected evidence schema")

    source_revision = value.get("source_revision")
    if not isinstance(source_revision, str) or not re.fullmatch(r"[0-9a-f]{40}", source_revision):
        fail("source_revision must be a full lowercase Git SHA-1")
    elif expected_source_revision is not None and source_revision != expected_source_revision:
        fail("source_revision does not match the qualified source revision")

    firmware_sha256 = value.get("firmware_sha256")
    if not isinstance(firmware_sha256, str) or not re.fullmatch(r"[0-9a-f]{64}", firmware_sha256):
        fail("firmware_sha256 must be lowercase SHA-256 hex")

    for key in ("board_model", "board_revision", "test_operator", "test_location"):
        if not isinstance(value.get(key), str) or not value[key].strip():
            fail(f"{key} must be a non-empty string")

    if value.get("idf_target") != "esp32p4":
        fail("idf_target must be esp32p4")
    if value.get("esp_idf_version") != "6.0.2":
        fail("esp_idf_version must be 6.0.2")
    if value.get("psram_enabled") is not True:
        fail("psram_enabled must be true")
    if value.get("sample_rate_hz") != 48000:
        fail("sample_rate_hz must be 48000")
    if value.get("overview_frames_per_column") != 32768:
        fail("overview_frames_per_column must be 32768")

    try:
        duration = _positive_int(value.get("duration_seconds"), "duration_seconds", MIN_DURATION_SECONDS)
        workspace = _positive_int(value.get("workspace_bytes"), "workspace_bytes", MIN_WORKSPACE_BYTES)
        result_pool = _positive_int(value.get("result_pool_bytes"), "result_pool_bytes", MIN_RESULT_POOL_BYTES)
        overview_columns = _positive_int(value.get("overview_columns"), "overview_columns")
        resident_beats = _positive_int(value.get("resident_beat_records"), "resident_beat_records")
        internal_before = _positive_int(value.get("internal_heap_free_before_bytes"), "internal_heap_free_before_bytes")
        internal_min = _positive_int(value.get("internal_heap_min_free_bytes"), "internal_heap_min_free_bytes")
        internal_after = _positive_int(value.get("internal_heap_free_after_bytes"), "internal_heap_free_after_bytes")
        psram_before = _positive_int(value.get("psram_free_before_bytes"), "psram_free_before_bytes")
        psram_min = _positive_int(value.get("psram_min_free_bytes"), "psram_min_free_bytes")
        psram_after = _positive_int(value.get("psram_free_after_bytes"), "psram_free_after_bytes")
        wall_duration = _positive_int(value.get("wall_duration_us"), "wall_duration_us")
        input_frames = _positive_int(value.get("input_frames"), "input_frames")
        input_bytes = _positive_int(value.get("input_bytes"), "input_bytes")
        input_callbacks = _positive_int(value.get("input_callbacks"), "input_callbacks")
        processed_frames = _positive_int(value.get("processed_frames"), "processed_frames")
        process_calls = _positive_int(value.get("process_calls"), "process_calls")
        process_average = _positive_int(value.get("process_call_average_us"), "process_call_average_us")
        process_p99 = _positive_int(value.get("process_call_p99_us"), "process_call_p99_us")
        process_max = _positive_int(value.get("process_call_max_us"), "process_call_max_us")
    except EvidenceError as exc:
        fail(str(exc))
        duration = workspace = result_pool = overview_columns = resident_beats = 0
        internal_before = internal_min = internal_after = 0
        psram_before = psram_min = psram_after = 0
        wall_duration = input_frames = input_bytes = input_callbacks = 0
        processed_frames = process_calls = process_average = 0
        process_p99 = process_max = 0

    if overview_columns > MAX_OVERVIEW_COLUMNS:
        fail(f"overview_columns exceeds {MAX_OVERVIEW_COLUMNS}")
    if resident_beats > MAX_RESIDENT_BEAT_RECORDS:
        fail(f"resident_beat_records exceeds {MAX_RESIDENT_BEAT_RECORDS}")
    if internal_min > internal_before:
        fail("internal_heap_min_free_bytes exceeds pre-test free heap")
    if internal_min > internal_after:
        fail("internal_heap_min_free_bytes exceeds post-test free heap")
    if psram_min > psram_before:
        fail("psram_min_free_bytes exceeds pre-test free PSRAM")
    if psram_min > psram_after:
        fail("psram_min_free_bytes exceeds post-test free PSRAM")
    if input_frames != EXPECTED_INPUT_FRAMES:
        fail(f"input_frames must equal {EXPECTED_INPUT_FRAMES}")
    if processed_frames != EXPECTED_INPUT_FRAMES:
        fail(f"processed_frames must equal {EXPECTED_INPUT_FRAMES}")
    if input_bytes != EXPECTED_INPUT_BYTES:
        fail(f"input_bytes must equal {EXPECTED_INPUT_BYTES}")
    if process_average > process_max:
        fail("process_call_average_us exceeds process_call_max_us")
    if process_p99 > process_max:
        fail("process_call_p99_us exceeds process_call_max_us")

    features = value.get("features")
    if not isinstance(features, list) or any(not isinstance(item, str) for item in features):
        fail("features must be an array of strings")
        feature_set: set[str] = set()
    else:
        feature_set = set(features)
        missing = sorted(REQUIRED_FEATURES - feature_set)
        if missing:
            fail(f"missing required features: {missing}")

    for counter in ("allocation_failure_count", "process_deadline_miss_count", "input_drop_count"):
        if value.get(counter) != 0:
            fail(f"{counter} must be 0")

    if value.get("usb_audio_coexistence_passed") is not True:
        fail("usb_audio_coexistence_passed must be true")
    if value.get("usb_stream_started") is not True:
        fail("usb_stream_started must be true")
    if value.get("usb_stream_completed") is not True:
        fail("usb_stream_completed must be true")
    if value.get("test_completed") is not True:
        fail("test_completed must be true")

    return {
        "schema": REPORT_SCHEMA,
        "source_revision": source_revision,
        "duration_seconds": duration,
        "workspace_bytes": workspace,
        "result_pool_bytes": result_pool,
        "overview_columns": overview_columns,
        "resident_beat_records": resident_beats,
        "wall_duration_us": wall_duration,
        "internal_heap_min_free_bytes": internal_min,
        "internal_heap_free_after_bytes": internal_after,
        "psram_min_free_bytes": psram_min,
        "psram_free_after_bytes": psram_after,
        "input_frames": input_frames,
        "input_bytes": input_bytes,
        "input_callbacks": input_callbacks,
        "processed_frames": processed_frames,
        "process_calls": process_calls,
        "process_call_average_us": process_average,
        "process_call_p99_us": process_p99,
        "process_call_max_us": process_max,
        "qualified_feature_count": len(feature_set & REQUIRED_FEATURES),
        "status": "pass" if not failures else "fail",
        "failures": failures,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--expected-source-revision")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        report = evaluate(load(args.evidence), args.expected_source_revision)
    except EvidenceError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 3
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, sort_keys=True))
    return 0 if report["status"] == "pass" else 2


if __name__ == "__main__":
    raise SystemExit(main())
