#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import sys
from pathlib import Path

MODEL_FORMAT = "apta-confidence-model-1"
REPORT_FORMAT = "apta-confidence-evaluation-1"
MIN_TRAIN = 96
MIN_HOLDOUT = 48
HIGH_CONFIDENCE = 75


class CalibrationError(ValueError):
    pass


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_rows(path: Path) -> list[dict[str, int | str]]:
    required = {"id", "raw_confidence", "correct"}
    rows: list[dict[str, int | str]] = []
    try:
        with path.open("r", encoding="utf-8", newline="") as source:
            reader = csv.DictReader(source)
            if reader.fieldnames is None or not required.issubset(reader.fieldnames):
                raise CalibrationError(f"{path}: missing columns {sorted(required - set(reader.fieldnames or []))}")
            for line, raw in enumerate(reader, 2):
                sample_id = (raw.get("id") or "").strip()
                if not sample_id:
                    raise CalibrationError(f"{path}:{line}: empty id")
                try:
                    confidence = int(raw["raw_confidence"])
                    correct = int(raw["correct"])
                except (TypeError, ValueError) as exc:
                    raise CalibrationError(f"{path}:{line}: invalid numeric value") from exc
                if not 0 <= confidence <= 100 or correct not in (0, 1):
                    raise CalibrationError(f"{path}:{line}: values out of range")
                rows.append({"id": sample_id, "raw_confidence": confidence, "correct": correct})
    except OSError as exc:
        raise CalibrationError(f"cannot read {path}: {exc}") from exc
    ids = [str(row["id"]) for row in rows]
    if len(ids) != len(set(ids)):
        raise CalibrationError(f"{path}: duplicate ids")
    return rows


def isotonic_lut(rows: list[dict[str, int | str]]) -> list[int]:
    if not rows:
        raise CalibrationError("training set is empty")
    grouped: dict[int, list[int]] = {}
    for row in rows:
        grouped.setdefault(int(row["raw_confidence"]), []).append(int(row["correct"]))
    blocks: list[dict[str, float | int]] = []
    for x in sorted(grouped):
        values = grouped[x]
        blocks.append({"lo": x, "hi": x, "sum": sum(values), "count": len(values)})
        while len(blocks) >= 2:
            a, b = blocks[-2], blocks[-1]
            if float(a["sum"]) / int(a["count"]) <= float(b["sum"]) / int(b["count"]):
                break
            blocks[-2:] = [{
                "lo": int(a["lo"]), "hi": int(b["hi"]),
                "sum": int(a["sum"]) + int(b["sum"]),
                "count": int(a["count"]) + int(b["count"]),
            }]
    fitted: dict[int, int] = {}
    for block in blocks:
        value = int(math.floor(100.0 * int(block["sum"]) / int(block["count"]) + 0.5))
        for x in range(int(block["lo"]), int(block["hi"]) + 1):
            fitted[x] = value
    known = sorted(fitted)
    lut: list[int] = []
    for x in range(101):
        if x in fitted:
            lut.append(fitted[x])
        elif x < known[0]:
            lut.append(fitted[known[0]])
        elif x > known[-1]:
            lut.append(fitted[known[-1]])
        else:
            lower = max(k for k in known if k < x)
            upper = min(k for k in known if k > x)
            lut.append(fitted[lower] if x - lower <= upper - x else fitted[upper])
    for index in range(1, 101):
        if lut[index] < lut[index - 1]:
            raise CalibrationError("internal error: non-monotone LUT")
    return lut


def canonical_model(rows: list[dict[str, int | str]]) -> dict[str, object]:
    lut = isotonic_lut(rows)
    training_ids = sorted(str(row["id"]) for row in rows)
    base = {
        "format": MODEL_FORMAT,
        "feature": "BPM",
        "method": "isotonic-pav-v1",
        "training_count": len(rows),
        "training_ids_sha256": sha256_bytes(("\n".join(training_ids) + "\n").encode()),
        "lut": lut,
    }
    canonical = json.dumps(base, sort_keys=True, separators=(",", ":")).encode()
    model_id = int.from_bytes(hashlib.sha256(canonical).digest()[:4], "big") or 1
    return {**base, "calibration_model_id": model_id}


def brier(confidences: list[int], outcomes: list[int]) -> float:
    return sum(((confidence / 100.0) - outcome) ** 2 for confidence, outcome in zip(confidences, outcomes)) / len(outcomes)


def ece(confidences: list[int], outcomes: list[int]) -> float:
    total = len(outcomes)
    result = 0.0
    for lower in range(0, 100, 10):
        upper = lower + 10
        indices = [i for i, value in enumerate(confidences) if lower <= value < upper or (upper == 100 and value == 100)]
        if not indices:
            continue
        mean_conf = sum(confidences[i] for i in indices) / (100.0 * len(indices))
        mean_acc = sum(outcomes[i] for i in indices) / len(indices)
        result += len(indices) / total * abs(mean_conf - mean_acc)
    return result


def metrics(confidences: list[int], outcomes: list[int]) -> dict[str, float | int]:
    return {
        "brier": round(brier(confidences, outcomes), 8),
        "ece_10_bin": round(ece(confidences, outcomes), 8),
        "high_confidence_errors": sum(c >= HIGH_CONFIDENCE and not y for c, y in zip(confidences, outcomes)),
        "mean_confidence": round(sum(confidences) / len(confidences), 6),
        "accuracy": round(sum(outcomes) / len(outcomes), 8),
    }


def evaluate(model: dict[str, object], training: list[dict[str, int | str]], holdout: list[dict[str, int | str]]) -> dict[str, object]:
    training_ids = {str(row["id"]) for row in training}
    holdout_ids = {str(row["id"]) for row in holdout}
    if training_ids & holdout_ids:
        raise CalibrationError("training and holdout IDs overlap")
    expected = canonical_model(training)
    if model != expected:
        raise CalibrationError("model does not match the supplied training set")
    lut = [int(value) for value in model["lut"]]
    raw = [int(row["raw_confidence"]) for row in holdout]
    calibrated = [lut[value] for value in raw]
    outcomes = [int(row["correct"]) for row in holdout]
    raw_metrics = metrics(raw, outcomes)
    calibrated_metrics = metrics(calibrated, outcomes)
    enough = len(training) >= MIN_TRAIN and len(holdout) >= MIN_HOLDOUT
    conditions = {
        "brier_not_worse": calibrated_metrics["brier"] <= raw_metrics["brier"],
        "ece_not_worse": calibrated_metrics["ece_10_bin"] <= raw_metrics["ece_10_bin"],
        "high_confidence_errors_not_worse": calibrated_metrics["high_confidence_errors"] <= raw_metrics["high_confidence_errors"],
    }
    benefits = {
        "brier_improved": calibrated_metrics["brier"] < raw_metrics["brier"],
        "ece_improved": calibrated_metrics["ece_10_bin"] < raw_metrics["ece_10_bin"],
        "high_confidence_errors_reduced": calibrated_metrics["high_confidence_errors"] < raw_metrics["high_confidence_errors"],
    }
    return {
        "format": REPORT_FORMAT,
        "calibration_model_id": int(model["calibration_model_id"]),
        "training_count": len(training),
        "holdout_count": len(holdout),
        "evidence_level": "acceptance" if enough else "diagnostic-only",
        "raw": raw_metrics,
        "calibrated": calibrated_metrics,
        "conditions": conditions,
        "benefits": benefits,
        "accepted": bool(enough and all(conditions.values()) and any(benefits.values())),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    train = sub.add_parser("train")
    train.add_argument("--input", required=True, type=Path)
    train.add_argument("--output", required=True, type=Path)
    check = sub.add_parser("evaluate")
    check.add_argument("--model", required=True, type=Path)
    check.add_argument("--training", required=True, type=Path)
    check.add_argument("--holdout", required=True, type=Path)
    check.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        if args.command == "train":
            model = canonical_model(read_rows(args.input))
            args.output.write_text(json.dumps(model, indent=2, sort_keys=True) + "\n", encoding="utf-8")
            return 0
        model = json.loads(args.model.read_text(encoding="utf-8"))
        report = evaluate(model, read_rows(args.training), read_rows(args.holdout))
        args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        return 0 if report["accepted"] else 2
    except (CalibrationError, OSError, json.JSONDecodeError) as exc:
        print(f"calibration error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
