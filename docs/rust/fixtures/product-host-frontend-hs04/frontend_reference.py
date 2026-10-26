# SPDX-License-Identifier: Apache-2.0
"""HS-04 direct mathematical DFT/feature oracle, not a candidate FFT or model.

No NumPy/madmom import, audio I/O or streaming/FFT scheduling implementation.
CS-02 coefficient/root/filter bytes are verified before use. Public artificial
signals only; full references reuse proven repeated frame equivalence classes.
"""
import functools
import hashlib
import math
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[4]
ASSETS = ROOT / "docs/rust/fixtures/product-compact-cs02/assets"
SIZE, HOP, BINS, BANDS = 1764, 441, 882, 144


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


@functools.lru_cache(maxsize=1)
def assets():
    manifest = __import__("json").loads((ASSETS / "manifest.json").read_text())
    result = {}
    for name in ["hann-1764.f64le", "dft-roots-1764.f64le", "filter-edge-bins.u16le",
                 "filter-starts.u16le", "filter-offsets.u32le", "filter-weights.f32le"]:
        raw = (ASSETS / name).read_bytes()
        e = manifest["assets"][name]
        if len(raw) != e["bytes"] or hashlib.sha256(raw).hexdigest() != e["sha256"]:
            raise ValueError("bound asset identity " + name)
        result[name] = tuple(x[0] for x in struct.iter_unpack("<" + e["scalar_format"], raw))
    return result


def sample(kind, j, length):
    if kind == "zero":
        return 0.0
    if kind == "dc1":
        return 1.0
    if kind == "dc2":
        return 2.0
    if kind == "dc-minus2":
        return -2.0
    if kind == "alternating2":
        return 2.0 if j % 2 == 0 else -2.0
    if kind == "pulse441":
        return 2.0 if j % 441 == 0 else 0.0
    if kind == "impulse0":
        return 2.0 if j == 0 else 0.0
    if kind == "impulse882":
        return 2.0 if j == 882 else 0.0
    if kind == "impulseEOF":
        return 2.0 if j == length - 1 else 0.0
    if kind == "step2":
        return -2.0 if j < length // 2 else 2.0
    if kind == "periodic257":
        return ((j * 17 + 3) % 257 - 128) / 64
    if kind == "cos7":
        return f32(2 * math.cos(2 * math.pi * 7 * (j % SIZE) / SIZE))
    raise ValueError("fixed mathematical signal only")


def direct_dft(values, bins=BINS):
    """Independent source-index sum using frozen roots and fsum, no butterfly."""
    if len(values) != SIZE or not all(math.isfinite(x) for x in values):
        raise ValueError("DFT geometry/finite")
    roots = assets()["dft-roots-1764.f64le"]
    nonzero = [(j, x) for j, x in enumerate(values) if x != 0]
    return [(math.fsum(x * roots[2 * ((j * k) % SIZE)] for j, x in nonzero),
             math.fsum(x * roots[2 * ((j * k) % SIZE) + 1] for j, x in nonzero))
            for k in range(bins)]


def frame_logs(values):
    if len(values) != SIZE or any(not math.isfinite(x) or abs(x) > 2 for x in values):
        raise ValueError("frame envelope")
    a = assets()
    windowed = [x * w for x, w in zip(values, a["hann-1764.f64le"])]
    spectrum = direct_dft(windowed)
    # Pinned STFT stores complex64 before magnitude, then f32 bands/log/diff.
    magnitude = [f32(math.hypot(f32(re), f32(im))) for re, im in spectrum]
    ideal_mag = [math.hypot(re, im) for re, im in spectrum]
    starts = a["filter-starts.u16le"]
    offsets = a["filter-offsets.u32le"]
    weights = a["filter-weights.f32le"]
    logs, ideal = [], []
    for b in range(BANDS):
        lo, hi, start = offsets[b], offsets[b + 1], starts[b]
        weighted = math.fsum(magnitude[start + j] * weights[lo + j] for j in range(hi - lo))
        logs.append(f32(math.log10(f32(1.0 + f32(weighted)))))
        weighted_ideal = math.fsum(ideal_mag[start + j] * weights[lo + j] for j in range(hi - lo))
        ideal.append(math.log10(1.0 + weighted_ideal))
    return logs, ideal


def centered_frame(case, row, source=None):
    k = case["samples"]
    first = row * HOP - SIZE // 2
    return [(source[j] if source is not None else sample(case["signal"], j, k))
            if 0 <= j < k else 0.0 for j in range(first, first + SIZE)]


def feature_rows(case, source=None):
    if not 0 <= case["samples"] <= 13230000:
        raise ValueError("input capacity")
    count = (case["samples"] + HOP - 1) // HOP
    previous = previous_ideal = None
    # Cache only the bounded equivalence classes in this oracle call.
    cache = {}
    for r in range(count):
        frame = centered_frame(case, r, source)
        raw = struct.pack("<" + "f" * SIZE, *frame)
        key = hashlib.sha256(raw).digest()
        if key not in cache:
            if len(cache) >= 16:
                raise ValueError("reference frame-class cap exceeded; not a full-song cache")
            cache[key] = frame_logs(frame)
        logs, ideal = cache[key]
        diff = [0.0] * BANDS if previous is None else [max(0.0, f32(x - y)) for x, y in zip(logs, previous)]
        ideal_diff = [0.0] * BANDS if previous_ideal is None else [max(0.0, x - y) for x, y in zip(ideal, previous_ideal)]
        yield r, logs + diff, ideal + ideal_diff
        previous, previous_ideal = logs, ideal


def geometry(samples, source_rate):
    source_frames = 0 if samples == 0 else ((samples - 1) * source_rate) // 22050 + 1
    return {"source_rate": source_rate, "source_frames": source_frames, "samples": samples,
            "rows": (samples + HOP - 1) // HOP}


def row_time(geometry, row):
    center = row * HOP
    required = min(geometry["samples"], center + SIZE // 2)
    if required == 0:
        source_required = 0
    else:
        source_center = (required - 1) * 2 if geometry["source_rate"] == 44100 else (required - 1) * 320 // 147
        source_required = min(geometry["source_frames"], source_center + 33)
    return {"row": row, "center_sample": center, "frame_first_sample": center - SIZE // 2,
            "frame_end_sample": center + SIZE // 2, "required_input_samples": required,
            "source_center_q32": str(row * geometry["source_rate"] * (1 << 32) // 50),
            "required_original_source_frames": source_required}
