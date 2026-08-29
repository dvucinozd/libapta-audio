#!/usr/bin/env python3
"""Unit checks for the pre-registered I9 oracle boundary."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile

import numpy as np


MODULE_PATH = (
    Path(__file__).resolve().parents[2]
    / "tools/apta_1_1_onset_trace_oracle.py"
)
SPEC = importlib.util.spec_from_file_location("apta_onset_trace_oracle", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def _trace(track: str, spectral: list[float]) -> dict[str, object]:
    count = len(spectral)
    captured = [1.0 if index % 94 == 0 else 0.0 for index in range(count)]
    return {
        "track": track,
        "sample_rate": 48000,
        "onset_evidence_first_bin": 0,
        "tempo_candidates": [],
        "onset_flux": captured,
        "onset_band_stride": 4,
        "onset_band_energy": [0.0] * (count * 4),
        "onset_spectral_flux_i9": spectral,
    }


def main() -> int:
    captured = np.asarray([0.0, 0.25, 0.75, 0.0], dtype=np.float64)
    spectral = np.asarray([0.5, 0.125, 0.5, 1.0], dtype=np.float64)
    fused = MODULE._spectral_flux_max_fusion(captured, spectral)
    if not np.array_equal(fused, np.asarray([0.5, 0.25, 0.75, 1.0])):
        raise AssertionError("I9 oracle formula is not exact element-wise max")

    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        traces = root / "traces"
        traces.mkdir()
        labels = [
            {
                "track": f"track-{index:04d}",
                "split": "development" if index <= 2 else "holdout",
                "beat_frames": list(range(0, 48_000 * 6, 24_000)),
            }
            for index in range(1, 5)
        ]
        labels_path = root / "labels.json"
        labels_path.write_text(json.dumps(labels), encoding="utf-8")
        spectral_trace = [
            0.5 if index % 94 == 1 else 0.0 for index in range(1024)
        ]
        for index in range(1, 3):
            path = traces / f"track-{index:04d}.ndjson"
            path.write_text(
                json.dumps(_trace(path.stem, spectral_trace)),
                encoding="utf-8",
            )

        report = MODULE.evaluate(
            labels_path,
            traces,
            "synthetic-i9-unit",
            include_i9_spectral=True,
        )
        if MODULE.I9_FORMULA_NAME not in report["formulas"]:
            raise AssertionError("I9 formula missing from oracle report")
        validation = report["validation"]
        if validation["i9_spectral_trace_tracks"] != 2:
            raise AssertionError("I9 trace coverage was not validated")

        broken = traces / "track-0001.ndjson"
        payload = json.loads(broken.read_text(encoding="utf-8"))
        payload["onset_spectral_flux_i9"] = [1.1] * 1024
        broken.write_text(json.dumps(payload), encoding="utf-8")
        try:
            MODULE.evaluate(
                labels_path,
                traces,
                "synthetic-i9-unit",
                include_i9_spectral=True,
            )
        except ValueError as exc:
            if "outside [0, 1]" not in str(exc):
                raise
        else:
            raise AssertionError("out-of-range I9 trace was accepted")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
