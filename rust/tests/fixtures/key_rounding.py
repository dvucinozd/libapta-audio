#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Diagnostic only: coefficient 27 rounding at the audited float32 argument.

Uses Decimal's Taylor series with a negligible truncation remainder; no external
numerical package, coefficient patch or DSP tolerance. Run alongside the compiled
C/Rust key audit, which establishes the actual signal and backend divergence.
"""
from decimal import Decimal, localcontext
import ctypes
import ctypes.util
import struct

with localcontext() as context:
    context.prec = 90
    argument = Decimal(struct.unpack(">f", bytes.fromhex("3ffa3924"))[0])
    term = Decimal(1)
    total = term
    for n in range(1, 100):
        term *= -argument * argument / Decimal((2 * n - 1) * (2 * n))
        total += term
    coefficient = 2 * total
    print("argument bits 3ffa3924; 100 Taylor terms; precision 90")
    print("coefficient", coefficient)
    print("last term magnitude", abs(term))
    for bits in ("bf3fd897", "bf3fd898"):
        value = Decimal(struct.unpack(">f", bytes.fromhex(bits))[0])
        print(bits, "absolute error", abs(value - coefficient))
    library = ctypes.CDLL(ctypes.util.find_library("m"))
    library.cosf.argtypes = [ctypes.c_float]
    library.cosf.restype = ctypes.c_float
    print("runtime cosf coefficient bits", struct.pack(">f", 2 * library.cosf(float(argument))).hex())
