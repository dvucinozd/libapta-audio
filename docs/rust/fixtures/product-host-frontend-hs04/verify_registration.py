# SPDX-License-Identifier: Apache-2.0
"""Independent HS-04 registration checks. Never imports reference/generator/candidate."""
from decimal import Decimal, localcontext
import hashlib
import json
import math
from pathlib import Path
import struct

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]


def strict_json(path):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("duplicate key " + key)
            result[key] = value
        return result
    def invalid(value):
        raise ValueError("nonfinite JSON constant " + value)
    return json.loads(path.read_text(), object_pairs_hook=unique, parse_constant=invalid)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check():
    f = strict_json(HERE / "cases.json")
    arrays = {}
    for name, entry in f["new_assets"].items():
        assert Path(name).name == name and entry["path"] == "assets/" + name
        path = HERE / entry["path"]
        raw = path.read_bytes()
        assert len(raw) == entry["bytes"] and digest(path) == entry["sha256"]
        values = tuple(x[0] for x in struct.iter_unpack("<" + entry["format"], raw))
        assert len(values) == entry["elements"] == math.prod(entry["shape"])
        assert all(math.isfinite(x) for x in values)
        arrays[name] = values
    assert {p.name for p in (HERE / "assets").iterdir()} == set(arrays)
    assert digest(HERE / "frontend_reference.py") == f["reference_sha256"]
    cs02 = ROOT / "docs/rust/fixtures/product-compact-cs02/assets"
    m = strict_json(cs02 / "manifest.json")
    frozen = {}
    for name in ["hann-1764.f64le", "dft-roots-1764.f64le", "filter-starts.u16le",
                 "filter-offsets.u32le", "filter-weights.f32le", "filter-edge-bins.u16le"]:
        e = m["assets"][name]
        assert digest(cs02 / name) == e["sha256"]
        frozen[name] = tuple(x[0] for x in struct.iter_unpack("<" + e["scalar_format"], (cs02 / name).read_bytes()))
    roots = frozen["dft-roots-1764.f64le"]
    primitive_anchors = 0
    with localcontext() as ctx:
        ctx.prec = 80
        for c in f["primitive_cases"]:
            x = arrays[Path(c["input"]["path"]).name]
            analytic = arrays[Path(c["analytic"]["path"]).name]
            direct = arrays[Path(c["direct"]["path"]).name]
            assert len(x) == 1764 and max(abs(v) for v in x) <= c["amplitude"]
            assert len(analytic) == len(direct) == 3528
            for k in range(1764):
                error = math.hypot(direct[2 * k] - analytic[2 * k], direct[2 * k + 1] - analytic[2 * k + 1])
                assert error <= 1e-4 + 2e-5 * math.hypot(analytic[2 * k], analytic[2 * k + 1])
            for k in [0, 1, 7, 881, 882, 1757, 1763]:
                for component in [0, 1]:
                    exact = sum((Decimal.from_float(v) * Decimal.from_float(roots[2 * ((j * k) % 1764) + component])
                                 for j, v in enumerate(x) if v), Decimal(0))
                    assert abs(float(exact) - direct[2 * k + component]) <= 1e-9
                    primitive_anchors += 1
    short_ids = [c["id"] for c in f["short_cases"]]
    assert len(short_ids) == len(set(short_ids)) == 20
    rows_checked, band_anchors = 0, 0
    hann = frozen["hann-1764.f64le"]
    starts, offsets, weights = (frozen[n] for n in ["filter-starts.u16le", "filter-offsets.u32le", "filter-weights.f32le"])
    def f32(x):
        return struct.unpack("<f", struct.pack("<f", x))[0]
    for c in f["short_cases"]:
        samples = arrays[Path(c["input"]["path"]).name]
        width = arrays[Path(c["features"]["path"]).name]
        ideal = arrays[Path(c["ideal_features"]["path"]).name]
        assert len(samples) == c["samples"] == (c["source_frames"] * 22050 + c["source_rate"] - 1) // c["source_rate"]
        assert c["rows"] == (len(samples) + 440) // 441
        assert len(width) == len(ideal) == c["rows"] * 288
        assert all(abs(x) <= 2 for x in samples)
        assert all(0 <= x < 4 and abs(x - y) <= 2e-5 + 2e-5 * abs(y) for x, y in zip(width, ideal))
        for r, time in enumerate(c["row_times"]):
            assert time["row"] == r and time["center_sample"] == r * 441
            assert time["frame_first_sample"] == r * 441 - 882 and time["frame_end_sample"] == r * 441 + 882
            q = min(len(samples), r * 441 + 882)
            required = min(c["source_frames"], (2 * (q - 1) if c["source_rate"] == 44100 else (q - 1) * 320 // 147) + 33) if q else 0
            assert time["required_input_samples"] == q and time["required_original_source_frames"] == required
            assert int(time["source_center_q32"]) == r * c["source_rate"] * (1 << 32) // 50
            logs, differences = width[r * 288:r * 288 + 144], width[r * 288 + 144:(r + 1) * 288]
            prev = width[(r - 1) * 288:(r - 1) * 288 + 144]
            expected_diff = (0.0,) * 144 if r == 0 else tuple(max(0.0, f32(x - y)) for x, y in zip(logs, prev))
            assert tuple(differences) == expected_diff
            # Independent selected bands from input samples, fixed window and roots.
            if r in (0, c["rows"] - 1):
                nonzero = [(n, samples[j] * hann[n]) for n in range(1764)
                           for j in [r * 441 - 882 + n] if 0 <= j < len(samples) and samples[j] * hann[n] != 0]
                for band in [0, 71, 143]:
                    terms = []
                    for j in range(offsets[band + 1] - offsets[band]):
                        k = starts[band] + j
                        re = math.fsum(v * roots[2 * ((n * k) % 1764)] for n, v in nonzero)
                        im = math.fsum(v * roots[2 * ((n * k) % 1764) + 1] for n, v in nonzero)
                        terms.append(f32(math.hypot(f32(re), f32(im))) * weights[offsets[band] + j])
                    expected = f32(math.log10(f32(1 + f32(math.fsum(terms)))))
                    assert abs(expected - logs[band]) <= 2e-5 + 2e-5 * abs(expected)
                    band_anchors += 1
            rows_checked += 1
    with localcontext() as ctx:
        ctx.prec = 90
        maximum = Decimal(0)
        for fs in [44100, 48000]:
            raw = (cs02 / f"fir-{fs}.f64le").read_bytes()
            e = m["assets"][f"fir-{fs}.f64le"]
            assert hashlib.sha256(raw).hexdigest() == e["sha256"]
            coefficients = [v[0] for v in struct.iter_unpack("<d", raw)]
            l1 = max(sum((Decimal.from_float(abs(x)) for x in coefficients[start:start + 65]), Decimal(0))
                     for start in range(0, len(coefficients), 65))
            assert l1 == Decimal(f["amplitude_envelope"]["FIR_phase_max_L1_decimal"][str(fs)])
            maximum = max(maximum, l1)
        u = Decimal(2) ** -53
        bound = maximum * (1 + 130 * u / (1 - 130 * u)) * (1 + Decimal(2) ** -24) + Decimal(2) ** -149
        assert bound == Decimal(f["amplitude_envelope"]["rounding_bound_decimal"]) and bound < 2
    full_rows = 0
    for c in f["full_cases"]:
        assert c["samples"] == 13230000 and c["rows"] == 30000 and c["source_frames"] == 600 * c["source_rate"]
        assert len(c["classes"]) <= 16 and c["complex_MACs"] == 1270080000
        def source_sample(j):
            kind = c["signal"]
            if kind == "zero":
                return 0.0
            if kind == "dc2":
                return 2.0
            if kind == "pulse441":
                return 2.0 if j % 441 == 0 else 0.0
            if kind == "alternating2":
                return 2.0 if j % 2 == 0 else -2.0
            raise AssertionError("unregistered full source")
        # Prove full input bytes from a separately expanded fundamental period.
        period = 441 if c["signal"] == "pulse441" else 2 if c["signal"] == "alternating2" else 1
        tile = b"".join(struct.pack("<f", source_sample(j)) for j in range(period))
        ih = hashlib.sha256(struct.pack("<IQQ", c["source_rate"], c["source_frames"], c["samples"]))
        count, tail = divmod(c["samples"], period)
        block = tile * 1024
        for _ in range(count // 1024):
            ih.update(block)
        ih.update(tile * (count % 1024))
        ih.update(tile[:tail * 4])
        assert ih.hexdigest() == c["input_sha256"]
        for row in [0, 1, 2, 3, 15000, 15001, 29997, 29998, 29999]:
            rep = str(row if row < 2 else 29999 if row == 29999 else 2 + row % 2)
            entry = c["classes"][c["class_by_representative"][rep]]
            frame = [source_sample(j) if 0 <= j < c["samples"] else 0.0
                     for j in range(441 * row - 882, 441 * row + 882)]
            assert hashlib.sha256(struct.pack("<1764f", *frame)).hexdigest() == entry["input_frame_sha256"]
        # All remaining interior frames have identical pulse residues, or the
        # parity represented by2/3; r>=2 and r<=R-2 implies no padding.
        assert 2 * 441 - 882 == 0 and (30000 - 2) * 441 + 882 == c["samples"]
        current, previous = None, None
        h, hi = hashlib.sha256(), hashlib.sha256()
        cache = {}
        for row in range(30000):
            rep = str(row if row < 2 else 29999 if row == 29999 else 2 + row % 2)
            current = c["class_by_representative"][rep]
            key = (previous, current)
            if key not in cache:
                entry = c["classes"][current]
                logs = arrays[Path(entry["logs"]["path"]).name]
                ideal = arrays[Path(entry["ideal_logs"]["path"]).name]
                prev = (arrays[Path(c["classes"][previous]["logs"]["path"]).name] if previous is not None else None)
                iprev = (arrays[Path(c["classes"][previous]["ideal_logs"]["path"]).name] if previous is not None else None)
                diff = [0.0] * 144 if prev is None else [max(0.0, f32(x - y)) for x, y in zip(logs, prev)]
                idiff = [0.0] * 144 if iprev is None else [max(0.0, x - y) for x, y in zip(ideal, iprev)]
                x, y = list(logs) + diff, list(ideal) + idiff
                assert all(abs(a - b) <= 2e-5 + 2e-5 * abs(b) for a, b in zip(x, y))
                cache[key] = (struct.pack("<288f", *x), struct.pack("<288d", *y))
            raw, iraw = cache[key]
            h.update(raw)
            hi.update(iraw)
            previous = current
            full_rows += 1
        assert h.hexdigest() == c["reference_features_sha256"] and hi.hexdigest() == c["reference_ideal_sha256"]
    return {"status": "STATIC_REGISTRATION_CONSISTENCY_PASS", "new_assets": len(arrays),
            "asset_bytes": sum(e["bytes"] for e in f["new_assets"].values()), "primitive_cases": len(f["primitive_cases"]),
            "independent_Decimal_complex_anchors": primitive_anchors, "short_feature_rows": rows_checked,
            "independent_band_anchors": band_anchors, "full_reference_rows_hashed": full_rows,
            "FIR_to_frontend_bound": str(bound), "candidate_implementation_tests_or_measurement": "NOT_RUN"}


if __name__ == "__main__":
    print(json.dumps(check(), sort_keys=True))
