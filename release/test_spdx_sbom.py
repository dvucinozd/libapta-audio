#!/usr/bin/env python3
"""Self-test for the deterministic release SPDX generator."""

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path


def run_generator(script: Path, release_root: Path, output: Path) -> dict:
    subprocess.run(
        [
            sys.executable,
            str(script),
            "--root",
            str(release_root),
            "--name",
            "libapta-audio",
            "--version",
            "9.8.7",
            "--source-revision",
            "0123456789abcdef0123456789abcdef01234567",
            "--created-epoch",
            "1767225600",
            "--output",
            str(output),
        ],
        check=True,
    )
    return json.loads(output.read_text(encoding="utf-8"))


def main() -> int:
    script = Path(__file__).with_name("generate_spdx_sbom.py").resolve()
    with tempfile.TemporaryDirectory() as temporary:
        base = Path(temporary)
        release_root = base / "assets"
        release_root.mkdir()
        (release_root / "alpha.txt").write_text("alpha\n", encoding="utf-8")
        nested = release_root / "nested"
        nested.mkdir()
        (nested / "beta.bin").write_bytes(b"\x00\x01\x02")

        first_path = base / "first.spdx.json"
        second_path = base / "second.spdx.json"
        first = run_generator(script, release_root, first_path)
        second = run_generator(script, release_root, second_path)

        assert first_path.read_bytes() == second_path.read_bytes()
        assert first == second
        assert first["spdxVersion"] == "SPDX-2.3"
        assert first["dataLicense"] == "CC0-1.0"
        assert len(first["files"]) == 2
        assert len(first["relationships"]) == 2
        for entry in first["files"]:
            algorithms = {item["algorithm"] for item in entry["checksums"]}
            assert algorithms == {"SHA1", "SHA256"}

        original_code = first["packages"][0]["packageVerificationCode"][
            "packageVerificationCodeValue"
        ]
        assert len(original_code) == hashlib.sha1().digest_size * 2

        (release_root / "alpha.txt").write_text("changed\n", encoding="utf-8")
        changed_path = base / "changed.spdx.json"
        changed = run_generator(script, release_root, changed_path)
        changed_code = changed["packages"][0]["packageVerificationCode"][
            "packageVerificationCodeValue"
        ]
        assert changed_code != original_code

    print("deterministic SPDX SBOM self-test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
