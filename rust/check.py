#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Combined native/C checks; generated data stays in the supplied build root."""
import argparse
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


def run(args, *, env=None):
    print("+", " ".join(str(a) for a in args), flush=True)
    subprocess.run([str(a) for a in args], check=True, cwd=ROOT, env=env)


ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-root", type=Path, required=True)
    parser.add_argument("--c-build", type=Path, help="reuse/configure this C build directory")
    parser.add_argument("--jobs", type=int, default=2, help="maximum concurrent build/test jobs")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    build = args.build_root.resolve()
    build.mkdir(parents=True, exist_ok=True)
    c_build = args.c_build.resolve() if args.c_build else build / "c-reference"
    run(["cmake", "-S", ROOT, "-B", c_build, "-DCMAKE_BUILD_TYPE=Release",
         "-DAPTA_BUILD_TESTS=ON", "-DAPTA_BUILD_EXAMPLES=ON", "-DAPTA_WARNINGS_AS_ERRORS=ON"])
    run(["cmake", "--build", c_build, "--parallel", str(args.jobs)])
    run(["ctest", "--test-dir", c_build, "--output-on-failure", f"-j{args.jobs}"])
    # Oracle is strictly test-only: the Rust library never links this archive.
    oracle = build / "waveform-oracle"
    run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
         "-I", ROOT / "include", ROOT / "rust/tests/fixtures/waveform_oracle.c",
         c_build / "libapta.a", "-lm", "-o", oracle])
    container_oracle = build / "container-oracle"
    run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
         "-I", ROOT / "include", ROOT / "rust/tests/fixtures/container_oracle.c",
         c_build / "libapta.a", "-lm", "-o", container_oracle])
    validator = c_build / "tools/apta-validate"
    stream_oracle = build / "stream-oracle"
    run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
         "-I", ROOT / "include", ROOT / "rust/tests/fixtures/stream_oracle.c",
         c_build / "libapta.a", "-lm", "-o", stream_oracle])
    native_result_oracle = build / "native-result-oracle"
    run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
         "-I", ROOT / "include", ROOT / "rust/tests/fixtures/native_result_oracle.c",
         c_build / "libapta.a", "-lm", "-o", native_result_oracle])
    publication_oracle = build / "publication-oracle"
    run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
         "-I", ROOT / "include", ROOT / "rust/tests/fixtures/publication_oracle.c",
         c_build / "libapta.a", "-lm", "-o", publication_oracle])
    sparse_oracle = build / "sparse-oracle"
    run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
         "-I", ROOT / "include", ROOT / "rust/tests/fixtures/sparse_oracle.c",
         c_build / "libapta.a", "-lm", "-o", sparse_oracle])
    scheduler_oracle = build / "scheduler-oracle"
    run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
         "-I", ROOT / "include", ROOT / "rust/tests/fixtures/scheduler_oracle.c",
         c_build / "libapta.a", "-lm", "-o", scheduler_oracle])
    sparse_pull_oracle = build / "sparse-pull-oracle"
    run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
         "-I", ROOT / "include", ROOT / "rust/tests/fixtures/sparse_pull_oracle.c",
         c_build / "libapta.a", "-lm", "-o", sparse_pull_oracle])
    seed_oracle = build / "seed-oracle"
    run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
         "-I", ROOT / "include", ROOT / "rust/tests/fixtures/seed_oracle.c",
         c_build / "libapta.a", "-lm", "-o", seed_oracle])
    analysis_oracles = {}
    for name in ("clock", "band", "detail_analysis", "detail_session", "detail_scheduler", "detail_pull", "tempo_analysis"):
        executable = build / f"{name.replace('_', '-')}-oracle"
        run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
             "-I", ROOT / "include", "-I", ROOT / "src/core",
             ROOT / f"rust/tests/fixtures/{name}_oracle.c",
             c_build / "libapta.a", "-lm", "-o", executable])
        analysis_oracles[f"APTA_C_{name.upper()}_ORACLE"] = str(executable)
    env = os.environ.copy()
    env.update(analysis_oracles)
    env["CARGO_BUILD_JOBS"] = str(args.jobs)
    env["RUST_TEST_THREADS"] = str(args.jobs)
    env["APTA_C_WAVEFORM_ORACLE"] = str(oracle)
    env["APTA_C_VALIDATOR"] = str(validator)
    env["APTA_C_CONTAINER_ORACLE"] = str(container_oracle)
    env["APTA_C_STREAM_ORACLE"] = str(stream_oracle)
    env["APTA_C_NATIVE_RESULT_ORACLE"] = str(native_result_oracle)
    env["APTA_C_PUBLICATION_ORACLE"] = str(publication_oracle)
    env["APTA_C_SPARSE_ORACLE"] = str(sparse_oracle)
    env["APTA_C_SCHEDULER_ORACLE"] = str(scheduler_oracle)
    env["APTA_C_SPARSE_PULL_ORACLE"] = str(sparse_pull_oracle)
    env["APTA_C_SEED_ORACLE"] = str(seed_oracle)
    env["CARGO_TARGET_DIR"] = str(build / "cargo-target")
    run(["cargo", "fmt", "--all", "--", "--check"], env=env)
    run(["cargo", "clippy", "--workspace", "--all-targets", "--locked", "--", "-D", "warnings"], env=env)
    run(["cargo", "test", "--workspace", "--locked"], env=env)
    run(["cargo", "test", "--workspace", "--locked", "--", "--ignored"], env=env)
    run(["cargo", "check", "--workspace", "--lib", "--no-default-features", "--locked"], env=env)
    run(["cargo", "test", "--workspace", "--release", "--locked"], env=env)
    run(["cargo", "test", "--workspace", "--release", "--locked", "--", "--ignored"], env=env)
    run(["cargo", "build", "--example", "wav_to_apta", "--locked"], env=env)
    executable = build / "cargo-target/debug/examples/wav_to_apta"
    # Original synthetic PCM, no private audio or external corpus involved.
    with tempfile.TemporaryDirectory(prefix="wav-smoke-", dir=build) as temp:
        temp = Path(temp)
        for tag, bits in [(1, 16), (1, 24), (1, 32), (3, 32)]:
            for channels in (1, 2):
                data = bytearray()
                for i in range(70001 * channels):
                    value = ((i * 127) % 65536) - 32768
                    if bits == 16:
                        data.extend(struct.pack("<h", value))
                    elif bits == 24:
                        data.extend((value << 8).to_bytes(3, "little", signed=True))
                    elif tag == 1:
                        data.extend(struct.pack("<i", value << 16))
                    else:
                        data.extend(struct.pack("<f", value / 32768))
                align = channels * (bits // 8)
                fmt = struct.pack("<HHIIHH", tag, channels, 48000, 48000 * align, align, bits)
                body = b"WAVEfmt " + struct.pack("<I", 16) + fmt + b"data" + struct.pack("<I", len(data)) + data
                if len(data) & 1:
                    body += b"\0"
                wav = temp / f"{tag}-{bits}-{channels}.wav"
                apta = wav.with_suffix(".apta")
                wav.write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)
                run([executable, wav, apta], env=env)
                run([validator, apta, "--strict"], env=env)
                # Demonstration must not overwrite an existing output.
                before = apta.read_bytes()
                result = subprocess.run([str(executable), str(wav), str(apta)], capture_output=True)
                if result.returncode == 0 or apta.read_bytes() != before:
                    raise RuntimeError("example overwrote existing output")
        # Retain one small public result for manual inspection, not a large corpus.
        shutil.copyfile(apta, build / "smoke-waveform.apta")
    print("Combined Rust/C checks and eight WAV interchange cases passed.")


if __name__ == "__main__":
    main()
