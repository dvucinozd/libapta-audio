#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path

_parts = [
    "part00.part",
    "part01.part",
    "part02.part",
    "part03.part",
    "part04.part",
]
_source = b"".join((Path(__file__).with_name("generate_container_v1_conformance.parts") / name).read_bytes() for name in _parts)
exec(compile(_source, str(Path(__file__)), "exec"), globals())
