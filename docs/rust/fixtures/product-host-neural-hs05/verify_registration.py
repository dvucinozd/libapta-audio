# SPDX-License-Identifier: Apache-2.0
"""Independent HS-05 static checks. Imports neither oracle nor generator/kernel."""
from decimal import Decimal, localcontext
import hashlib
import json
import math
from pathlib import Path
import struct

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]


def read_json(path):
    def unique(pairs):
        result = {}
        for k, v in pairs:
            if k in result:
                raise ValueError("duplicate JSON key")
            result[k] = v
        return result
    def bad(v):
        raise ValueError("nonfinite JSON constant " + v)
    return json.loads(path.read_text(), object_pairs_hook=unique, parse_constant=bad)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def b32(x):
    return struct.unpack("<I", struct.pack("<f", float(x)))[0]


def value(bits):
    return struct.unpack("<f", struct.pack("<I", bits))[0]


def q(x, decimal=False):
    result = value(b32(x))
    return Decimal.from_float(result) if decimal else result


def near(x, y):
    x, y = float(x), float(y)
    assert math.isfinite(x) and math.isfinite(y)
    assert abs(x-y) <= 2e-6+2e-5*abs(y), (x, y)


def activation(x, kind, decimal):
    if decimal:
        if kind == "tanh":
            e = (2*x).exp()
            result = (e-1)/(e+1)
        elif x >= 0:
            result = 1/(1+(-x).exp())
        else:
            e = x.exp()
            result = e/(1+e)
    elif kind == "tanh":
        result = math.tanh(x)
    else:
        e = math.exp(-x if x >= 0 else x)
        result = 1/(1+e) if x >= 0 else e/(1+e)
    return q(result, decimal)


def affine(x, flat, bias, decimal=False):
    result = []
    width = len(x)
    for r, b in enumerate(bias):
        total = q(0, decimal)
        for j in range(width):
            term = q(flat[width*r+j]*x[j], decimal)
            total = q(total+term, decimal)
        result.append(q(total+b, decimal))
    return result


def one_cell(w, layer, x, h, c, decimal=False):
    base = 8 + 48*layer
    # Independent interleaved input/recurrent dot walks. Each accumulator still
    # obeys the registered ascending-input-index f32 order and post-dot bias.
    ih, hh, z, gates = [], [], [], []
    for gate in range(8):
        a, b = q(0, decimal), q(0, decimal)
        for j in range(2):
            a = q(a+q(x[j]*w[base+2*gate+j], decimal), decimal)
            b = q(b+q(h[j]*w[base+16+2*gate+j], decimal), decimal)
        ih.append(q(a+w[base+32+gate], decimal))
        hh.append(q(b+w[base+40+gate], decimal))
        z.append(q(ih[-1]+hh[-1], decimal))
        gates.append(activation(z[-1], "tanh" if gate//2 == 2 else "sigmoid", decimal))
    cells = []
    hidden = []
    for j in range(2):
        cells.append(q(q(gates[2+j]*c[j], decimal)+q(gates[j]*gates[4+j], decimal), decimal))
        hidden.append(q(gates[6+j]*activation(cells[-1], "tanh", decimal), decimal))
    return hidden, cells, {"ih": ih, "hh": hh, "z": z, "gates": gates}


def probabilities(logits, decimal=False):
    m = max(logits)
    delta = [q(x-m, decimal) for x in logits]
    exponent = [q(x.exp() if decimal else math.exp(x), decimal) for x in delta]
    total = q(0, decimal)
    for x in exponent:
        total = q(total+x, decimal)
    return [q(x/total, decimal) for x in exponent], {"delta": delta, "exp": exponent, "sum": [total]}


def evaluate(w, x, state, decimal=False):
    lin = affine(x, w[:6], w[6:8], decimal)
    h0, c0, _ = one_cell(w, 0, lin, state[:2], state[4:6], decimal)
    h1, c1, _ = one_cell(w, 1, h0, state[2:4], state[6:8], decimal)
    logits = affine(h1, w[104:110], w[110:113], decimal)
    p, _ = probabilities(logits, decimal)
    return p+h0+h1+c0+c1


def signal(name, r):
    if name == "zero":
        return [0., 0., 0.]
    if name == "periodic17":
        return [((5*r+3*j+1) % 17-8)/8 for j in range(3)]
    if name == "impulse":
        return [.75, -.5, .25] if r == 0 else [0., 0., 0.]
    sign = (-1.)**(r % 2)
    assert name == "alternating"
    return [sign, -sign, sign/2]


def check():
    f = read_json(HERE / "cases.json")
    arrays = {}
    for name, e in f["raw_assets"].items():
        assert Path(name).name == name and e["path"] == "assets/"+name
        path = HERE/e["path"]
        assert path.stat().st_size == e["bytes"] and digest(path) == e["sha256"]
        arrays[e["path"]] = [v[0] for v in struct.iter_unpack("<f", path.read_bytes())]
        assert all(math.isfinite(v) for v in arrays[e["path"]])
    assert len(arrays) == 28 and sum(e["bytes"] for e in f["raw_assets"].values()) == 6848
    for e in list(f["profiles"].values()) + [e[k] for e in f["short_cases"] for k in ("input", "trace")]:
        assert e["dtype"] == "f32le" and math.prod(e["shape"]) == e["elements"] == len(arrays[e["path"]])
        assert e["bytes"] == 4*e["elements"] and digest(HERE/e["path"]) == e["sha256"]
    profiles = {k: arrays[e["path"]] for k, e in f["profiles"].items()}
    assert set(profiles) == {"zero", "asymmetric", "gate-tagged", "rounding"}
    assert all(len(v) == 113 and all(abs(x) <= 2 for x in v) for v in profiles.values())
    assert all(b32(x) == 0 for x in profiles["zero"])
    # Verify the rational coefficient cycles without importing/reusing recipes.
    k = -4
    for x in profiles["asymmetric"]:
        assert x == k/32
        k += 7
        if k > 9:
            k -= 19
    k = -4
    for x in profiles["rounding"]:
        assert b32(x) == b32(Decimal(k)/10)
        k += 11
        if k > 11:
            k -= 23
    assert profiles["gate-tagged"][:8] == [1.,0.,0.,0.,1.,0.,0.,0.]
    assert profiles["gate-tagged"][104:] == [1.,.25,-.5,1.,.25,-.75,.125,-.25,.375]
    for layer, base in enumerate([8, 56]):
        for r in range(8):
            for j in range(2):
                assert profiles["gate-tagged"][base+2*r+j] == (((r+1)*(j+2)+3*layer) % 11-5)/8
                assert profiles["gate-tagged"][base+16+2*r+j] == (((r+3)*(j+1)+layer) % 9-4)/16
        ih_bias = [.5,-.5,1.,-1.,.25,-.25,1.5,-1.5]
        hh_bias = [.125,-.125,-.25,.25,-.125,.125,.5,-.5]
        assert profiles["gate-tagged"][base+32:base+40] == [v/(1+layer) for v in ih_bias]
        assert profiles["gate-tagged"][base+40:base+48] == (hh_bias[::-1] if layer else hh_bias)
    off = 0
    for e in f["profile_layout"]:
        assert e["offset_f32"] == off and e["elements"] == math.prod(e["shape"])
        off += e["elements"]
    assert off == 113
    primitive_scalars = 0
    for c in f["primitive_cases"]:
        kind = c["kind"]
        x = [Decimal.from_float(value(b)) for b in (c["input_bits"] if isinstance(c["input_bits"], list) else [c["input_bits"]])]
        if kind == "linear":
            got = affine(x, [Decimal.from_float(value(b)) for b in c["weight_bits"]], [Decimal.from_float(value(b)) for b in c["bias_bits"]], True)
            assert [b32(v) for v in got] == c["expected_bits"]
        elif kind == "activation":
            got = [activation(x[0], k, True) for k in ["sigmoid", "tanh"]]
            e = x[0].exp()
            ideal = [e/(1+e), ((2*x[0]).exp()-1)/((2*x[0]).exp()+1)]
            for a, b in zip(c["ideal_f64"], ideal):
                assert abs(a-float(b)) <= 4e-15
        elif kind == "softmax":
            got, inter = probabilities(x, True)
            for key, vals in inter.items():
                for a, b in zip(vals, c["intermediate_bits"][key]):
                    if key == "delta":
                        assert b32(a) == b
                    else:
                        near(value(b), a)
                    primitive_scalars += 1
            assert max(range(3), key=lambda k: got[k]) == c["argmax"]
            assert abs(sum(float(v) for v in got)-1) <= 2e-6
            if len(set(c["input_bits"])) == 1:
                assert c["expected_bits"] == [b32(1/3)]*3
        else:
            assert kind == "cell"
            w = [Decimal.from_float(v) for v in profiles[c["profile"]]]
            h, cell, inter = one_cell(w, c["layer"], x, [Decimal.from_float(value(b)) for b in c["h_bits"]], [Decimal.from_float(value(b)) for b in c["c_bits"]], True)
            got = h+cell
            for key in ["ih", "hh", "z"]:
                assert [b32(v) for v in inter[key]] == c["intermediate_bits"][key]
                primitive_scalars += len(inter[key])
            for a, b in zip(inter["gates"], c["intermediate_bits"]["gates"]):
                near(value(b), a)
                primitive_scalars += 1
        for a, b in zip(got, c["expected_bits"]):
            near(value(b), a)
            primitive_scalars += 1
    assert next(c for c in f["primitive_cases"] if c["id"] == "linear-round-order")["expected_bits"] == [b32(0), b32(-1)]
    assert next(c for c in f["primitive_cases"] if c["id"] == "linear-bias-after")["expected_bits"] == [b32(1), b32(-1.5)]
    assert set(c["argmax"] for c in f["primitive_cases"] if c["kind"] == "softmax") == {0,1,2}
    assert primitive_scalars == 302
    decimal_rows = 0
    for c in f["short_cases"]:
        w = [Decimal.from_float(v) for v in profiles[c["profile"]]]
        state = [Decimal(0)]*8
        inputs, trace = arrays[c["input"]["path"]], arrays[c["trace"]["path"]]
        assert len(inputs) == c["rows"]*3 and len(trace) == c["rows"]*11
        for r in range(c["rows"]):
            if r in c["reset_before_rows"]:
                state = [Decimal(0)]*8
            x = [Decimal.from_float(v) for v in inputs[3*r:3*r+3]]
            assert [float(v) for v in x] == signal(c["signal"], r)
            got = evaluate(w, x, state, True)
            for a, b in zip(trace[11*r:11*r+11], got):
                near(a, b)
            assert max(range(3), key=lambda k: got[k]) == max(range(3), key=lambda k: trace[11*r+k])
            state = got[3:]
            decimal_rows += 1
        expected = c["expected"]
        assert hashlib.sha256(struct.pack("<Q", c["rows"])+struct.pack("<"+"f"*len(inputs), *inputs)).hexdigest() == expected["input_sha256"]
        assert digest(HERE/c["trace"]["path"]) == expected["trace_sha256"]
    full_rows = 0
    for c in f["full_cases"]:
        state = [0.]*8
        ih = hashlib.sha256(struct.pack("<Q", c["rows"]))
        th = hashlib.sha256()
        argmax = [0, 0, 0]
        for r in range(c["rows"]):
            if r in c["reset_before_rows"]:
                state = [0.]*8
            x = signal(c["signal"], r)
            got = evaluate(profiles[c["profile"]], x, state)
            state = got[3:]
            ih.update(struct.pack("<3f", *x)); th.update(struct.pack("<11f", *got))
            argmax[max(range(3), key=lambda k: got[k])] += 1
            if str(r) in c["expected"]["anchors_bits"]:
                assert [b32(v) for v in got] == c["expected"]["anchors_bits"][str(r)]
            assert all(math.isfinite(v) for v in got) and all(0 <= v <= 1 for v in got[:3])
            assert all(abs(v) <= 1 for v in got[3:7]) and all(abs(v) <= r+1 for v in got[7:])
            full_rows += 1
        assert ih.hexdigest() == c["expected"]["input_sha256"] and th.hexdigest() == c["expected"]["trace_sha256"]
        assert argmax == c["expected"]["argmax_counts"]
        assert c["expected"]["macs"] == 76*c["rows"] and c["expected"]["step_units"] == 172*c["rows"]
        assert c["expected"]["reset_units"] == len(c["reset_before_rows"])
    p = read_json(ROOT/"docs/rust/PRODUCT-HOST-NEURAL-PROTOCOL.json")
    for name, sha in p["authority_sha256"].items():
        assert digest(ROOT/name) == sha, name
    for k in ["document", "reference", "fixtures", "registration_verifier"]:
        assert digest(ROOT/p[k]["path"]) == p[k]["sha256"], k
    old_asset_count = 0
    for field in ["frozen_frontend_assets", "frozen_CS02_assets"]:
        bucket = p[field]
        for name, e in bucket["assets"].items():
            path = ROOT/bucket["root"]/e.get("path", name)
            assert path.stat().st_size == e["bytes"] and digest(path) == e["sha256"], name
            old_asset_count += 1
    assert p["measured_order"] == f["measured_order"] and len(p["measured_order"]) == 5
    assert sum(p["work_units_per_row"].values()) == 172
    assert p["maximum_total_units"] >= 30000*172+1
    assert p["storage"]["payload_f32"] == 64 and p["storage"]["payload_bytes"] == 256
    assert decimal_rows == 90 and full_rows == 90000 and len(f["primitive_cases"]) == 34
    return {"status": "STATIC_REGISTRATION_CONSISTENCY_PASS", "assets": len(arrays), "asset_bytes": 6848,
            "independent_Decimal_primitive_scalars": primitive_scalars, "independent_Decimal_sequence_rows": decimal_rows,
            "independent_full_scalar_rows": full_rows, "frozen_prior_assets_checked": old_asset_count,
            "frozen_authority_files_checked": len(p["authority_sha256"]), "candidate_implementation_or_measurement": "NOT_RUN"}


if __name__ == "__main__":
    with localcontext() as context:
        context.prec = 100
        print(json.dumps(check(), sort_keys=True))
