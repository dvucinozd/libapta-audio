# SPDX-License-Identifier: Apache-2.0
"""HS-03 public scalar oracle. No Rust implementation, audio files or model imports.

Short cases use a direct source-index convolution. Full periodic cases hash one
infinite-periodic interior tile plus independently zero-padded finite boundaries.
No whole-song PCM/output allocation; stored CS-02 coefficient bytes are authority.
"""
import functools
import hashlib
import math
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[4]
ASSETS = ROOT / "docs/rust/fixtures/product-compact-cs02/assets"


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


@functools.lru_cache(maxsize=2)
def coefficients(fs):
    expected = {44100: "0ea7547740cd8bb98d5e1241def77faeed99f3dad5b60a5b89e187e3045b1cd6",
                48000: "0abf15aae616314f39a8c732562225561ade41a37db638e484121ce8a48411b3"}
    if fs not in expected:
        raise ValueError("unsupported source rate")
    raw = (ASSETS / f"fir-{fs}.f64le").read_bytes()
    count = 1 if fs == 44100 else 147
    if len(raw) != count * 65 * 8 or hashlib.sha256(raw).hexdigest() != expected[fs]:
        raise ValueError("coefficient shape")
    values = struct.unpack("<" + "d" * (count * 65), raw)
    if not all(math.isfinite(x) for x in values):
        raise ValueError("nonfinite coefficient")
    return tuple(values[p * 65:(p + 1) * 65] for p in range(count))


def position(fs, k):
    return (2 * k, 0) if fs == 44100 else divmod(320 * k, 147)


def signal(case, j, channel):
    kind = case["signal"]
    if kind == "zero":
        return 0.0
    if kind == "negative-zero":
        return -0.0
    if kind == "dc":
        return 1.0
    if kind in ("impulse", "left-impulse", "right-impulse"):
        active = kind == "impulse" or channel == (0 if kind == "left-impulse" else 1)
        return float(active and j == case["impulse_index"])
    if kind == "alternating":
        return 1.0 if j % 2 == 0 else -1.0
    if kind == "step":
        return -1.0 if j < case["frames"] // 2 else 1.0
    if kind in ("periodic", "cancel-pair"):
        if kind == "cancel-pair":
            value = ((j * 17 + 3) % 257 - 128) / 128
            return value if channel == 0 else -value
        return ((j * (17 if channel == 0 else 29) + (3 if channel == 0 else 11)) % 257 - 128) / 128
    raise ValueError("unregistered signal")


def mono(case, j):
    left = signal(case, j, 0)
    return left if case["channels"] == 1 else f32(f32(left + signal(case, j, 1)) * 0.5)


def direct_sample(case, k, stable=False):
    center, phase = position(case["sample_rate"], k)
    row = coefficients(case["sample_rate"])[phase]
    terms = []
    accumulator = 0.0
    for tap in range(65):
        j = center + tap - 32
        value = mono(case, j) if 0 <= j < case["frames"] else 0.0
        product = value * row[tap]
        if stable:
            terms.append(product)
        else:
            accumulator = accumulator + product
    return f32(math.fsum(terms) if stable else accumulator)


def input_hash(case):
    h = hashlib.sha256(struct.pack("<IBQ", case["sample_rate"], case["channels"], case["frames"]))
    if case["signal"] in ("periodic", "cancel-pair"):
        tile = b"".join(struct.pack("<f", signal(case, j, ch)) for j in range(257) for ch in range(case["channels"]))
        count, tail = divmod(case["frames"], 257)
        for _ in range(count):
            h.update(tile)
        h.update(tile[:tail * case["channels"] * 4])
    else:
        for j in range(case["frames"]):
            for ch in range(case["channels"]):
                h.update(struct.pack("<f", signal(case, j, ch)))
    return h.hexdigest()


def output_count(case):
    return (case["frames"] * 22050 + case["sample_rate"] - 1) // case["sample_rate"]


def short_oracle(case):
    if not 0 <= case["frames"] <= 4097 or case["channels"] not in (1, 2):
        raise ValueError("short oracle shape/capacity")
    count = output_count(case)
    values = [direct_sample(case, k) for k in range(count)]
    stable = [direct_sample(case, k, True) for k in range(count)]
    error = max((abs(x - y) for x, y in zip(values, stable)), default=0.0)
    if any(abs(x - y) > 2e-5 + 2e-5 * abs(y) for x, y in zip(values, stable)):
        raise ValueError("ordered reference exceeds frozen mathematical tolerance")
    return {"output_count": count, "output_sha256": hashlib.sha256(b"".join(struct.pack("<f", x) for x in values)).hexdigest(),
            "first_bits": [struct.pack("<f", x).hex() for x in values[:8]],
            "last_bits": [struct.pack("<f", x).hex() for x in values[-8:]],
            "minimum": min(values) if values else None, "maximum": max(values) if values else None,
            "ordered_vs_fsum_max_abs": error, "input_sha256": input_hash(case),
            "status": "Done" if count else "NoEvidence"}


def periodic_tile(case):
    period = 257 if case["sample_rate"] == 44100 else 147 * 257
    source = [mono(case, j) for j in range(257)]
    rows = coefficients(case["sample_rate"])
    data = bytearray()
    for k in range(period):
        n, phase = position(case["sample_rate"], k)
        acc = 0.0
        for tap in range(65):
            acc = acc + source[(n + tap - 32) % 257] * rows[phase][tap]
        data.extend(struct.pack("<f", f32(acc)))
    return bytes(data)


def full_oracle(case):
    if case["signal"] != "periodic" or case["frames"] < 4097:
        raise ValueError("full oracle only registered periodic long cases")
    fs = case["sample_rate"]
    count = output_count(case)
    tile = periodic_tile(case)
    period = len(tile) // 4
    first = (32 * 22050 + fs - 1) // fs
    end = ((case["frames"] - 32) * 22050 + fs - 1) // fs
    h = hashlib.sha256()
    for k in range(first):
        h.update(struct.pack("<f", direct_sample(case, k)))
    k = first
    while k < end:
        offset = k % period
        take = min(end - k, period - offset)
        h.update(tile[offset * 4:(offset + take) * 4])
        k += take
    for k in range(end, count):
        h.update(struct.pack("<f", direct_sample(case, k)))
    return {"output_count": count, "output_sha256": h.hexdigest(), "input_sha256": input_hash(case),
            "status": "Done", "oracle_period_outputs": period,
            "interior_first_output": first, "interior_end_output": end}
