# SPDX-License-Identifier: Apache-2.0
"""HS-05 mathematical toy oracle: no trained weights, framework or candidate.

Pure scalar functions only; no streaming/work scheduling/kernel implementation.
All nonlinear calls use F64 math then the explicitly specified f32 boundary.
"""
import hashlib
import math
import struct

INPUTS, HIDDEN, LAYERS, CLASSES, PARAMETERS = 3, 2, 2, 3, 113
LAYOUT = [
    ("linear.weight", [2, 3], 0, 6), ("linear.bias", [2], 6, 2),
    ("l0.weight_ih", [8, 2], 8, 16), ("l0.weight_hh", [8, 2], 24, 16),
    ("l0.bias_ih", [8], 40, 8), ("l0.bias_hh", [8], 48, 8),
    ("l1.weight_ih", [8, 2], 56, 16), ("l1.weight_hh", [8, 2], 72, 16),
    ("l1.bias_ih", [8], 88, 8), ("l1.bias_hh", [8], 96, 8),
    ("head.weight", [3, 2], 104, 6), ("head.bias", [3], 110, 3),
]


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def from_bits(value):
    return struct.unpack("<f", struct.pack("<I", value))[0]


def packed(values):
    return b"".join(struct.pack("<f", x) for x in values)


def artificial_parameters(name):
    """Public arithmetic recipes, not checkpoint extraction or random training."""
    if name == "zero":
        return [0.0] * PARAMETERS
    if name == "rounding":
        return [f32(((n * 11 + 7) % 23 - 11) / 10) for n in range(PARAMETERS)]
    values = [f32(((n * 7 + 5) % 19 - 9) / 32) for n in range(PARAMETERS)]
    if name == "asymmetric":
        return values
    if name != "gate-tagged":
        raise ValueError("fixed artificial profile only")
    values[:8] = [1., 0., 0., 0., 1., 0., 0., 0.]
    bih = [.5, -.5, 1., -1., .25, -.25, 1.5, -1.5]
    bhh = [.125, -.125, -.25, .25, -.125, .125, .5, -.5]
    for layer, base in enumerate([8, 56]):
        for r in range(8):
            for j in range(2):
                values[base + 2*r+j] = f32((((r+1)*(j+2)+3*layer) % 11-5)/8)
                values[base+16+2*r+j] = f32((((r+3)*(j+1)+layer) % 9-4)/16)
            values[base+32+r] = f32(bih[r]/(layer+1))
            values[base+40+r] = bhh[7-r] if layer else bhh[r]
    values[104:] = [1., .25, -.5, 1., .25, -.75, .125, -.25, .375]
    return values


def dot(x, w):
    if len(x) != len(w):
        raise ValueError("dot shape")
    result = 0.0
    for value, weight in zip(x, w):
        product = f32(value * weight)
        result = f32(result + product)
    return result


def dense(x, w, bias):
    if len(w) != len(x)*len(bias):
        raise ValueError("dense shape")
    return [f32(dot(x, w[r*len(x):(r+1)*len(x)]) + b) for r, b in enumerate(bias)]


def sigmoid(x):
    if x >= 0:
        e = math.exp(-float(x))
        return f32(1.0 / (1.0 + e))
    e = math.exp(float(x))
    return f32(e / (1.0 + e))


def tanh(x):
    return f32(math.tanh(float(x)))


def cell(parameters, layer, x, h, c):
    if len(parameters) != 113 or layer not in (0, 1) or any(len(v) != 2 for v in [x, h, c]):
        raise ValueError("cell shape")
    base = [8, 56][layer]
    ih = dense(x, parameters[base:base+16], parameters[base+32:base+40])
    hh = dense(h, parameters[base+16:base+32], parameters[base+40:base+48])
    z = [f32(a+b) for a, b in zip(ih, hh)]
    gates = [tanh(v) if 4 <= j < 6 else sigmoid(v) for j, v in enumerate(z)]
    next_c = [f32(f32(gates[2+j]*c[j]) + f32(gates[j]*gates[4+j])) for j in range(2)]
    next_h = [f32(gates[6+j]*tanh(next_c[j])) for j in range(2)]
    return next_h, next_c, {"ih": ih, "hh": hh, "z": z, "gates": gates}


def softmax(logits):
    if len(logits) != 3 or not all(math.isfinite(v) for v in logits):
        raise ValueError("softmax finite/shape")
    maximum = max(logits)
    delta = [f32(v-maximum) for v in logits]
    exponent = [f32(math.exp(float(v))) for v in delta]
    total = 0.0
    for value in exponent:
        total = f32(total+value)
    probabilities = [f32(v/total) for v in exponent]
    return probabilities, {"delta": delta, "exp": exponent, "sum": [total]}


def row(parameters, x, state):
    """state = h0[2],h1[2],c0[2],c1[2]. Return fresh state, never mutate input."""
    if len(x) != 3 or len(state) != 8 or len(parameters) != 113:
        raise ValueError("toy row shape")
    linear = dense(x, parameters[:6], parameters[6:8])
    h0, c0, _ = cell(parameters, 0, linear, state[:2], state[4:6])
    h1, c1, _ = cell(parameters, 1, h0, state[2:4], state[6:8])
    logits = dense(h1, parameters[104:110], parameters[110:113])
    probabilities, _ = softmax(logits)
    return probabilities, h0+h1+c0+c1


def input_row(signal, index):
    if signal == "zero":
        return [0.0]*3
    if signal == "periodic17":
        return [f32(((index*5+j*3+1) % 17-8)/8) for j in range(3)]
    if signal == "impulse":
        return [.75, -.5, .25] if index == 0 else [0.0]*3
    if signal == "alternating":
        sign = 1 if index % 2 == 0 else -1
        return [float(sign), float(-sign), .5*sign]
    raise ValueError("fixed mathematical input only")


def reference_stream(case, parameters):
    state = [0.0]*8
    resets = set(case["reset_before_rows"])
    for r in range(case["rows"]):
        if r in resets:
            state = [0.0]*8
        x = input_row(case["signal"], r)
        probabilities, state = row(parameters, x, state)
        yield r, x, probabilities+state


def stream_identity(case, parameters):
    inputs = hashlib.sha256(struct.pack("<Q", case["rows"]))
    trace = hashlib.sha256()
    anchors = {}
    selected = {0, 1, 2, 14999, 15000, 15001, case["rows"]-1}
    argmax = [0, 0, 0]
    for r, x, values in reference_stream(case, parameters):
        inputs.update(packed(x))
        trace.update(packed(values))
        winner = max(range(3), key=lambda k: values[k])
        argmax[winner] += 1
        if r in selected:
            anchors[str(r)] = [bits(v) for v in values]
    return {"input_sha256": inputs.hexdigest(), "trace_sha256": trace.hexdigest(),
            "anchors_bits": anchors, "argmax_counts": argmax,
            "macs": case["rows"]*76, "step_units": case["rows"]*172,
            "reset_units": len(case["reset_before_rows"])}
