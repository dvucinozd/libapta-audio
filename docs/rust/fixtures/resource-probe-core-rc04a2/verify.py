"""Frozen public mathematical oracle verification; no candidate imports/runs."""
import decimal
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import struct

root = Path(__file__).resolve().parent
book = json.loads((root / 'oracles.json').read_text())
for name, bound in book['assets'].items():
    raw = (root / name).read_bytes()
    assert len(raw) == bound['bytes'] and hashlib.sha256(raw).hexdigest() == bound['sha256']

# Repeated-add modular recurrence, not the generator's direct multiplication.
expected_copy = []
word = 0
for _ in range(16384):
    expected_copy.append(word)
    word = (word + 2654435761) % (2**32)
assert list(struct.unpack('<16384I', (root/'copy.u32le').read_bytes())) == expected_copy

# Independently reconstruct all dot terms as rational vectors with modular
# recurrences. Check exact dyadic representability and both serialized hashes.
expected_projection = []
for row in range(16):
    x = []
    state = row % 17
    for _ in range(150):
        x.append(Fraction(state - 8, 32))
        state = (state + 3) % 17
    for column in range(150):
        state = (7 * column) % 19
        dot = Fraction(0)
        for value in x:
            dot += value * Fraction(state - 9, 64)
            state = (state + 5) % 19
        numerator = dot * 2048
        assert numerator.denominator == 1
        expected_projection.append(int(numerator))
        assert Fraction(struct.unpack('<f', struct.pack('<f', float(dot)))[0]) == dot
        assert Fraction(float(dot)) == dot
assert list(struct.unpack('<2400i', (root/'projection.i32le').read_bytes())) == expected_projection
for dtype in ['f', 'd']:
    raw = b''.join(struct.pack('<'+dtype, n/2048) for n in expected_projection)
    assert hashlib.sha256(raw).hexdigest() == book['projection']['f32_sha256' if dtype == 'f' else 'f64_sha256']

D = decimal.Decimal
with decimal.localcontext() as context:
    context.prec = 120
    # Symmetry-based stable sigmoid and tanh identity are independent formulas.
    refs = []
    for x in [-20, -2, -1, 0, 1, 2, 20]:
        e = D(-abs(x)).exp()
        refs.append(float(e/(1+e) if x < 0 else 1/(1+e)))
    for x in [-20, -2, -1, 0, 1, 2, 20]:
        e = D(-2*abs(x)).exp()
        magnitude = (1-e)/(1+e)
        refs.append(float(-magnitude if x < 0 else magnitude))
    values = [struct.unpack('<f', struct.pack('<I', bits))[0] for bits in book['log_input_bits']]
    for divisor in [1, 15]:
        for value in values:
            # Division must first round to binary64, as required by HS06.
            argument = value/divisor
            refs.append(float(D.from_float(argument).ln()) if value else float('-inf'))
    stored = struct.unpack('<28d', (root/'nonlinear.f64le').read_bytes())
    for index, (got, reference) in enumerate(zip(stored, refs)):
        if reference == float('-inf'):
            assert got == reference
        else:
            # Reference generation's high-precision p/15 and actual binary64
            # division differ only below the existing 4e-13 gate.
            assert abs(got-reference) <= (4e-13 if index >= 14 else 1e-15)
print(json.dumps(dict(status='PUBLIC_MATH_AND_IDENTITIES_PASS', candidate='NOT_RUN',
                     copy_words=16384, projection_outputs=2400, nonlinear_scalars=28)))
