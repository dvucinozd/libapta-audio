# SPDX-License-Identifier: Apache-2.0
"""Independent CS-02 asset checks; never imports the generator/model or reads music."""
import argparse
from decimal import Decimal, localcontext, ROUND_FLOOR
from fractions import Fraction
import hashlib
from itertools import product
import json
import math
from pathlib import Path
import struct

PI = Decimal("3.141592653589793238462643383279502884197169399375105820974944592307816406286208998628")
I = tuple(range(10, 76))


def trig(x, cosine=False):
    x -= (x / (2 * PI)).to_integral_value(rounding=ROUND_FLOOR) * (2 * PI)
    if x > PI:
        x -= 2 * PI
    term = Decimal(1) if cosine else x
    total = term
    for n in range(1, 180):
        denominator = (2 * n - 1) * (2 * n) if cosine else (2 * n) * (2 * n + 1)
        term *= -(x * x) / denominator
        total += term
        if abs(term) < Decimal("1e-85"):
            return total
    raise AssertionError("Taylor convergence")


def verify(directory):
    def unique(pairs):
        result = {}
        for k, v in pairs:
            assert k not in result, k
            result[k] = v
        return result

    def read_json(path):
        def invalid_constant(value):
            raise AssertionError("nonfinite JSON constant: " + value)
        return json.loads(path.read_bytes(), object_pairs_hook=unique,
                          parse_constant=invalid_constant)

    m = read_json(directory / "manifest.json")
    assert m["format"] == "libapta-compact-cs02-assets-1"
    assert m["source_base"] == "76b4f2aa9570b808633eaba5d9d385fd9f9ebe15"
    spec = read_json(Path(__file__).resolve().parents[2] / "PRODUCT-COMPACT-CANDIDATE-SPEC.json")
    arrays = {}
    for name, entry in m["assets"].items():
        assert Path(name).name == name
        raw = (directory / name).read_bytes()
        assert len(raw) == entry["bytes"] and hashlib.sha256(raw).hexdigest() == entry["sha256"], name
        if "scalar_format" in entry:
            fmt = entry["scalar_format"]
            values = [v[0] for v in struct.iter_unpack("<" + fmt, raw)]
            assert len(values) == entry["elements"] == math.prod(entry["shape"]), name
            assert all(math.isfinite(x) for x in values), name
            arrays[name] = values
    assert sum(x["bytes"] for x in m["assets"].values()) == m["total_asset_bytes"]
    assert hashlib.sha256((directory.parent / "generate_assets.py").read_bytes()).hexdigest() == m["generator_sha256"]
    assert set(p.name for p in directory.iterdir()) == set(m["assets"]) | {"manifest.json"}

    worst_fir, worst_hann, worst_roots = 0.0, 0.0, 0.0
    with localcontext() as ctx:
        ctx.prec = 80
        for rate, phases, chosen in [(44100, 1, [0]), (48000, 147, [0, 1, 26, 73, 146])]:
            data = arrays[f"fir-{rate}.f64le"]
            for p in range(phases):
                assert abs(math.fsum(data[p * 65:(p + 1) * 65]) - 1) <= 2e-14
            for p in chosen:
                raw = []
                for offset in range(-32, 33):
                    z = Decimal(p) / phases - offset
                    c = Decimal(22050) / rate
                    arg = PI * c * z
                    sinc = Decimal(1) if arg == 0 else trig(arg) / arg
                    raw.append(c * sinc * (1 + trig(PI * z / 33, True)) / 2)
                denominator = sum(raw)
                for j, x in enumerate(raw):
                    error = abs(data[p * 65 + j] - float(x / denominator))
                    worst_fir = max(worst_fir, error)
                    assert error <= 2e-14
        hann = arrays["hann-1764.f64le"]
        assert hann[0] == hann[-1] == 0
        assert max(abs(x - y) for x, y in zip(hann, reversed(hann))) <= 2e-15
        assert abs(math.fsum(hann) - 881.5) <= 2e-12
        for n in [0, 1, 440, 881, 882, 1762, 1763]:
            expected = (1 - trig(2 * PI * n / 1763, True)) / 2
            error = abs(hann[n] - float(expected))
            worst_hann = max(worst_hann, error)
            assert error <= 2e-14
        roots = arrays["dft-roots-1764.f64le"]
        for k in range(1764):
            a, b = roots[2 * k:2 * k + 2]
            assert abs(a * a + b * b - 1) <= 2e-15
        for k in [0, 1, 3, 7, 441, 882, 1323, 1763]:
            for part, cosine in [(0, True), (1, False)]:
                expected = trig(-2 * PI * k / 1764, cosine)
                error = abs(roots[2 * k + part] - float(expected))
                worst_roots = max(worst_roots, error)
                assert error <= 2e-14

        # Independent exact/high-precision transition masks and normalization.
        transition = {}
        for old in I:
            row = [(-100 * abs(Decimal(new) / old - 1)).exp() for new in I]
            row = [Decimal(0) if p <= Decimal(2) ** -52 else p for p in row]
            normalizer = sum(row)
            for new, p in zip(I, row):
                if p:
                    transition[old, new] = (p / normalizer).ln()
        assert len(transition) == 1741
        worst_logp = 0.0
        for meter in (3, 4):
            prefix = f"hmm{meter}-"
            ptr = arrays[prefix + "pointers.u32le"]
            pred = arrays[prefix + "previous.u16le"]
            lp = arrays[prefix + "logp.f64le"]
            triples = arrays[prefix + "state-map.u8"]
            size = meter * 2805
            assert len(ptr) == size + 1 and ptr[0] == 0 and ptr[-1] == len(pred) == len(lp)
            assert len(triples) == 3 * size

            def index(beat, interval, q):
                return beat * 2805 + (interval - 10) * (interval + 9) // 2 + q

            for state in range(size):
                beat, interval, q = triples[3 * state:3 * state + 3]
                assert 0 <= beat < meter and 10 <= interval <= 75 and 0 <= q < interval
                assert index(beat, interval, q) == state
                start, stop = ptr[state:state + 2]
                assert start < stop
                actual = pred[start:stop]
                assert actual == sorted(set(actual)) and all(0 <= p < size for p in actual)
                if q:
                    assert actual == [state - 1] and lp[start:stop] == [0.0]
                else:
                    expected = [(index((beat - 1) % meter, old, old - 1), transition[old, interval])
                                for old in I if (old, interval) in transition]
                    expected.sort()
                    assert actual == [p for p, _ in expected]
                    for k, (_, logarithm) in enumerate(expected, start):
                        error = abs(lp[k] - float(logarithm))
                        worst_logp = max(worst_logp, error)
                        assert error <= 4e-13
            assert len(pred) == meter * (2805 - 66 + 1741)
            assert m["graphs"][str(meter)]["graph_and_maps_bytes"] == (
                (size + 1) * 4 + len(pred) * 10 + size * 3)

    # Exact rational triangles provide a different reference from the f32 reducer.
    edge = arrays["filter-edge-bins.u16le"]
    assert edge == spec["static_metadata"]["frontend_geometry"]["unique_edge_bins"]
    starts = arrays["filter-starts.u16le"]
    offsets = arrays["filter-offsets.u32le"]
    weights = arrays["filter-weights.f32le"]
    assert len(edge) == 146 and edge == sorted(set(edge))
    assert len(starts) == 144 and len(offsets) == 145
    assert offsets[0] == 0 and offsets[-1] == len(weights)
    worst_filter = 0.0
    for band in range(144):
        lo, center, hi = edge[band:band + 3]
        assert starts[band] == lo
        values = weights[offsets[band]:offsets[band + 1]]
        assert len(values) == hi - lo and values[0] == 0
        area = Fraction(hi - lo, 2)
        exact = [Fraction(k - lo, center - lo) / area if k < center
                 else Fraction(hi - k, hi - center) / area for k in range(lo, hi)]
        assert all(v >= 0 for v in values)
        assert abs(math.fsum(values) - 1) <= 2**-21
        for value, expected in zip(values, exact):
            error = abs(value - float(expected))
            worst_filter = max(worst_filter, error)
            assert error <= 2**-21 * float(expected) + 2**-149

    oracles = read_json(directory / "oracles.json")
    assert len(oracles["tiny_hmm"]) == 4
    for case in oracles["tiny_hmm"]:
        tm = [[Fraction(x) for x in row] for row in case["transition"]]
        obs = [[Fraction(x) for x in row] for row in case["densities"]]
        paths = []
        for all_states in product(range(2), repeat=case["rows"] + 1):
            value = Fraction(1, 2)
            for t in range(case["rows"]):
                value *= tm[all_states[t]][all_states[t + 1]] * obs[t][all_states[t + 1]]
            paths.append((value, tuple(reversed(all_states[1:])) + (all_states[0],), all_states))
        top = min(paths, key=lambda x: (-x[0], x[1]))
        assert top[0] == Fraction(case["expected_probability"])
        assert list(top[2][1:]) == list(case["expected_path"])
        assert top[2][0] == case["expected_initial"]
    boundary = oracles["threshold"]
    assert float.fromhex(boundary["f32_at_decimal_005_hex"]) >= 0.05
    assert float.fromhex(boundary["preceding_f32_hex"]) < 0.05
    assert struct.pack("<f", 1 / 3).hex() == "abaaaa3e"

    return {"format": "libapta-compact-cs02-independent-validation-1", "result": "PASS",
            "source_base": m["source_base"], "asset_files": len(m["assets"]),
            "asset_bytes": m["total_asset_bytes"], "graphs": m["graphs"],
            "decimal_precision": 80, "reference_checks": {
                "fir_phases_all_65_coefficients": 6, "hann_points": 7, "dft_roots": 8,
                "transition_pairs": 66 * 66, "graph_states": 8415 + 11220,
                "filter_bands": 144, "exhaustive_tiny_hmm_cases": 4},
            "maximum_reference_errors": {"fir": worst_fir, "hann": worst_hann,
                                         "roots": worst_roots, "log_transition": worst_logp,
                                         "filter_f32_vs_exact_rational": worst_filter},
            "execution": {"model_frontend_or_candidate_decoder_run": False,
                          "audio_labels_logits_or_weights_read": False,
                          "device_run": False, "packages_installed": False}}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--receipt", type=Path, required=True)
    args = parser.parse_args()
    result = verify(args.assets)
    result["verifier_sha256"] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    result["manifest_sha256"] = hashlib.sha256((args.assets / "manifest.json").read_bytes()).hexdigest()
    with args.receipt.open("x") as stream:
        json.dump(result, stream, indent=2, sort_keys=True, allow_nan=False)
        stream.write("\n")
    print(json.dumps(result, indent=2))
