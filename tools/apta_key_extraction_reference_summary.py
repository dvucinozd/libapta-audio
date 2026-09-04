#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Summarize fixed synthetic numerical references, without accepting a detector."""
import argparse
import json
import math
from pathlib import Path
import re

from apta_key_mode_diagnostic_summary import aggregate, load_report

PREVIOUS_HASHES = {
    False: "fc82ddbf13c04a1d406e40c2e44f48573c820dd4d99454f7123635695b1d586c",
    True: "eedb1b97e4d975b37b925b649c80596871c7462412d3550de8d2139e862f06f1",
}


def summarize_rows(rows):
    pcm = [r for r in rows if r["kind"].startswith("pcm_")]
    if len(pcm) != 576:
        raise ValueError("expected 576 PCM rows")
    result = {}
    for name in ("effective", "nominal"):
        mapped, errors, identities = [], [], []
        changed = 0
        for row in pcm:
            ref = row["extraction_reference"][name]
            chroma = ref["chroma"]
            if (len(chroma) != 12 or any(type(x) not in (float, int) or not math.isfinite(x)
                                        or x < 0 for x in chroma) or max(chroma) <= 0):
                raise ValueError("invalid reference chroma")
            for key, limit in (("selected_tonic", 12), ("selected_mode", 2), ("confidence", 101)):
                if type(ref[key]) is not int or not 0 <= ref[key] < limit:
                    raise ValueError("invalid reference verdict")
            error = max(abs(a-b) for a, b in zip(chroma, row["chroma"])) / max(1.0, max(chroma))
            recorded = ref["max_chroma_error_over_max_reference"]
            identity = ref["max_fourier_goertzel_energy_error"]
            if (not math.isfinite(recorded) or not math.isclose(error, recorded, rel_tol=1e-3, abs_tol=1e-8)):
                # Native chroma is serialized to nine significant digits.
                raise ValueError("inconsistent chroma error metric")
            if not math.isfinite(identity) or not 0 <= identity <= 1e-6:
                raise ValueError("reference energy self-check failed")
            changed += (ref["selected_tonic"], ref["selected_mode"]) != (row["selected_tonic"], row["selected_mode"])
            mapped.append({**row, **{k: ref[k] for k in ("chroma", "selected_tonic", "selected_mode", "confidence")}})
            errors.append(recorded)
            identities.append(identity)
        result[name] = {
            "compared_pcm_rows": len(pcm), "changed_tonic_or_mode": changed,
            "max_chroma_error_over_max_reference": max(errors),
            "mean_chroma_error_over_max_reference": sum(errors) / len(errors),
            "max_fourier_goertzel_energy_error": max(identities),
            "groups": aggregate(mapped),
        }
    result["native_groups"] = aggregate(pcm)
    return result


def summarize_pair(report, previous, band):
    old, old_hash = load_report(previous, band)
    if old_hash != PREVIOUS_HASHES[band]:
        raise ValueError("frozen baseline report hash mismatch")
    rows, report_hash = load_report(report, band, "apta-key-extraction-reference-1")
    if [{k: v for k, v in r.items() if k != "extraction_reference"} for r in rows] != old:
        raise ValueError("native diagnostic observations changed")
    return {"report_sha256": report_hash, "baseline_report_sha256": old_hash,
            "native_rows_unchanged": True, **summarize_rows(rows)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("default_report", "band_report", "previous_default", "previous_band", "output"):
        parser.add_argument("--" + name.replace("_", "-"), type=Path, required=True)
    parser.add_argument("--native-source-revision", required=True)
    args = parser.parse_args()
    try:
        if not re.fullmatch(r"[0-9a-f]{40}", args.native_source_revision):
            raise ValueError("full native source revision required")
        report = {"format": "apta-key-extraction-reference-summary-1", "acceptance_claim": False,
                  "evidence_level": "synthetic-diagnostic", "native_source_revision": args.native_source_revision,
                  "default": summarize_pair(args.default_report, args.previous_default, False),
                  "semitone_band": summarize_pair(args.band_report, args.previous_band, True)}
        with args.output.open("x", encoding="utf-8", newline="\n") as output:
            output.write(json.dumps(report, sort_keys=True, indent=2, allow_nan=False) + "\n")
    except (OSError, ValueError, TypeError, KeyError) as exc:
        parser.exit(2, f"error: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
