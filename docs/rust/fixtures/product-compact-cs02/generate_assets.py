# SPDX-License-Identifier: Apache-2.0
"""CS-02 public mathematical assets; no audio, weights, neural or HMM execution.

The binary32 sum order is derived from NumPy v1.26.4; see NUMPY-NOTICE.txt.
Output must be a NEW directory. Frozen checked-in bytes, not platform libm
regeneration, are the future fixture authority. All arrays are raw little endian.
"""
import argparse
import bisect
from fractions import Fraction
import hashlib
from itertools import product
import json
import math
from pathlib import Path
import platform
import struct
import sys

BASE = "76b4f2aa9570b808633eaba5d9d385fd9f9ebe15"
NFFT = 1764
INTERVALS = tuple(range(10, 76))


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def sum_f32(values):
    """Fixed 8-lane NumPy 1.26.4 pairwise order, bounded to actual filter size."""
    n = len(values)
    assert 0 < n <= 128
    if n < 8:
        result = -0.0
        for value in values:
            result = f32(result + value)
        return result
    lanes = list(values[:8])
    end = n - n % 8
    for start in range(8, end, 8):
        for lane in range(8):
            lanes[lane] = f32(lanes[lane] + values[start + lane])
    pairs = [f32(lanes[i] + lanes[i + 1]) for i in (0, 2, 4, 6)]
    result = f32(f32(pairs[0] + pairs[1]) + f32(pairs[2] + pairs[3]))
    for value in values[end:]:
        result = f32(result + value)
    return result


def make_assets(directory):
    directory.mkdir(parents=True, exist_ok=False)
    entries = {}

    def emit(name, fmt, values, shape):
        values = list(values)
        raw = struct.pack("<" + fmt * len(values), *values)
        (directory / name).write_bytes(raw)
        entries[name] = {
            "sha256": hashlib.sha256(raw).hexdigest(), "bytes": len(raw),
            "scalar_format": fmt, "shape": shape, "elements": len(values),
        }
        return len(raw)

    def save_json(name, data):
        raw = (json.dumps(data, indent=2, sort_keys=True, allow_nan=False) + "\n").encode()
        (directory / name).write_bytes(raw)
        entries[name] = {"sha256": hashlib.sha256(raw).hexdigest(), "bytes": len(raw)}
        return len(raw)

    # CS-FIR65: table index is fractional phase p, not output sample index k.
    for source_rate, phases in ((44100, 1), (48000, 147)):
        coefficients = []
        cutoff = 22050 / source_rate
        for phase in range(phases):
            fraction = phase / phases
            row = []
            for offset in range(-32, 33):
                z = fraction - offset
                v = cutoff * z
                sinc = 1.0 if v == 0.0 else math.sin(math.pi * v) / (math.pi * v)
                row.append(cutoff * sinc * (1.0 + math.cos(math.pi * z / 33)) / 2)
            normalizer = sum(row)  # ascending offset, binary64
            coefficients.extend(value / normalizer for value in row)
        emit(f"fir-{source_rate}.f64le", "d", coefficients, [phases, 65])

    hann = [0.5 - 0.5 * math.cos(2 * math.pi * n / (NFFT - 1)) for n in range(NFFT)]
    emit("hann-1764.f64le", "d", hann, [NFFT])
    # Mathematical DFT reference roots. This is not an executed FFT kernel.
    roots = [(math.cos(-2 * math.pi * k / NFFT), math.sin(-2 * math.pi * k / NFFT))
             for k in range(NFFT)]
    emit("dft-roots-1764.f64le", "d", (x for pair in roots for x in pair), [NFFT, 2])

    # Pinned madmom bin mapping and triangular/filter dtype semantics.
    first = math.floor(24 * math.log2(30 / 440))
    last = math.ceil(24 * math.log2(17000 / 440))
    frequencies = [440 * 2 ** (k / 24) for k in range(first, last)]
    frequencies = [f for f in frequencies if 30 <= f <= 17000]
    bin_frequencies = [k * 22050 / NFFT for k in range(NFFT // 2)]
    bins = []
    for frequency in frequencies:
        k = min(max(bisect.bisect_left(bin_frequencies, frequency), 1), 881)
        if frequency - bin_frequencies[k - 1] < bin_frequencies[k] - frequency:
            k -= 1
        bins.append(k)
    bins = sorted(set(bins))
    assert len(bins) == 146
    starts, offsets, weights = [], [0], []
    for start, center, stop in zip(bins, bins[1:], bins[2:]):
        # Each integer-rational ramp is rounded to binary32 before normalization.
        a, b = center - start, stop - center
        raw = [f32(float(Fraction(k, a))) for k in range(a)]
        raw += [f32(float(1 - Fraction(k, b))) for k in range(b)]
        norm = sum_f32(raw)
        row = [f32(value / norm) for value in raw]
        starts.append(start)
        weights.extend(row)
        offsets.append(len(weights))
    assert len(starts) == 144
    emit("filter-edge-bins.u16le", "H", bins, [146])
    emit("filter-starts.u16le", "H", starts, [144])
    emit("filter-offsets.u32le", "I", offsets, [145])
    emit("filter-weights.f32le", "f", weights, [len(weights)])

    # Source-defined mathematical transition table. No Viterbi is executed.
    transition = {}
    for old in INTERVALS:
        row = [math.exp(-100 * abs(new / old - 1)) for new in INTERVALS]
        row = [0.0 if p <= 2**-52 else p for p in row]
        total = sum(row)
        for new, value in zip(INTERVALS, row):
            if value:
                transition[old, new] = math.log(value / total)
    graphs = {}
    for meter in (3, 4):
        states = [(beat, interval, q) for beat in range(meter)
                  for interval in INTERVALS for q in range(interval)]
        ids = {state: i for i, state in enumerate(states)}
        pointers, previous, log_prob = [0], [], []
        for beat, interval, q in states:
            if q:
                incoming = [(ids[beat, interval, q - 1], 0.0)]
            else:
                incoming = [(ids[(beat - 1) % meter, old, old - 1],
                             transition[old, interval])
                            for old in INTERVALS if (old, interval) in transition]
            incoming.sort()
            for pred, logp in incoming:
                previous.append(pred)
                log_prob.append(logp)
            pointers.append(len(previous))
        assert len(states) < 65535
        prefix = f"hmm{meter}-"
        nbytes = emit(prefix + "pointers.u32le", "I", pointers, [len(states) + 1])
        nbytes += emit(prefix + "previous.u16le", "H", previous, [len(previous)])
        nbytes += emit(prefix + "logp.f64le", "d", log_prob, [len(previous)])
        nbytes += emit(prefix + "state-map.u8", "B",
                       (x for state in states for x in state), [len(states), 3])
        graphs[str(meter)] = {"states": len(states), "edges": len(previous),
                              "graph_and_maps_bytes": nbytes}
    assert graphs["3"]["states"] == 8415 and graphs["4"]["states"] == 11220

    # Exhaustive rational tiny-graph examples, not a candidate decoder.
    tiny = []
    for count, transition_rows, densities in (
        (1, [[Fraction(1, 2)] * 2] * 2, [[Fraction(1, 2)] * 2]),
        (3, [[Fraction(1, 2)] * 2] * 2, [[Fraction(1, 2)] * 2] * 3),
        (3, [[Fraction(4, 5), Fraction(1, 5)],
             [Fraction(1, 5), Fraction(4, 5)]],
            [[Fraction(9, 10), Fraction(1, 10)],
             [Fraction(1, 10), Fraction(9, 10)],
             [Fraction(9, 10), Fraction(1, 10)]]),
        (8, [[Fraction(1, 2)] * 2] * 2, [[Fraction(1, 2)] * 2] * 8),
    ):
        best = None
        for trial in product(range(2), repeat=count + 1):
            initial, path = trial[0], trial[1:]
            probability, old = Fraction(1, 2), initial
            for row, state in enumerate(path):
                probability *= transition_rows[old][state] * densities[row][state]
                old = state
            # Lowest final state, recursively lowest predecessors.
            tie_key = tuple(reversed(path)) + (initial,)
            if best is None or probability > best[0] or (probability == best[0] and tie_key < best[1]):
                best = probability, tie_key, initial, path
        tiny.append({"rows": count,
                     "transition": [[str(x) for x in row] for row in transition_rows],
                     "densities": [[str(x) for x in row] for row in densities],
                     "expected_initial": best[2], "expected_path": best[3],
                     "expected_probability": str(best[0])})
    # f32 threshold examples and analytic FFT/zero-network values are literal oracles.
    boundary = f32(0.05)
    bits = struct.unpack("<I", struct.pack("<f", boundary))[0]
    before = struct.unpack("<f", struct.pack("<I", bits - 1))[0]
    oracles = {
        "kind": "public mathematical examples; no candidate NN/frontend/Viterbi execution",
        "tiny_hmm": tiny,
        "threshold": {"comparison_rhs_f64_hex": float(0.05).hex(),
                      "f32_at_decimal_005_hex": boundary.hex(),
                      "preceding_f32_hex": before.hex(),
                      "expected_at": True, "expected_before": False,
                      "row0_only_support": "range [0,1), not NO_EVIDENCE"},
        "probability_validation": {"sum_tolerance": "2^-21", "renormalize": False,
                                   "zero_triple": "INVALID_INPUT",
                                   "zero_beat_downbeat_nonbeat_one": "NO_EVIDENCE",
                                   "nonfinite_or_out_of_unit_interval": "INVALID_INPUT"},
        "fft_analytic": [
            {"input": "delta[n=0]", "expected_all_bins": [1, 0]},
            {"input": "constant real1, length1764", "bin0": [1764, 0], "other_bins": [0, 0]},
            {"input": "cos(2*pi*7*n/1764)", "bin7_and_1757": [882, 0], "other_bins": [0, 0]},
            {"input": "delta[n=3]", "bin_k": "root[(3*k)%1764]"},
        ],
        "zero_network_closed_form": {"one_scalar_LSTM_zero_state_zero_weights": {"h": 0, "c": 0},
                                     "three_zero_logits_softmax_f32_hex": f32(1 / 3).hex()},
        "resampler_phase": {"44100": "phase0; source center=2*k",
                            "48000": "phase=(320*k)%147; center_floor=(320*k)//147"},
    }
    save_json("oracles.json", oracles)
    manifest = {
        "format": "libapta-compact-cs02-assets-1", "source_base": BASE,
        "generator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "generation_runtime": {"python": sys.version, "platform": platform.platform()},
        "authority": "frozen file bytes; regeneration libm drift is not auto-accepted",
        "scalar_endian": "little; no header/padding",
        "coefficient_policy": "f64 FIR/Hann/DFT; f32 ramps and fixed NumPy1.26.4 pairwise filter normalization",
        "assets": entries, "graphs": graphs,
        "total_asset_bytes": sum(e["bytes"] for e in entries.values()),
        "execution": {"neural_model": False, "candidate_frontend_or_Viterbi": False,
                      "audio_labels_logits_or_weights_read": False, "packages_installed": False},
    }
    (directory / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = make_assets(args.output)
    print(json.dumps({"asset_bytes": result["total_asset_bytes"], "graphs": result["graphs"]}))
