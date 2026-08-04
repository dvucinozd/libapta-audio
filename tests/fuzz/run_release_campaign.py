#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run and verify the reproducible Stage S9 P7 libFuzzer campaign."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import subprocess
import time
from pathlib import Path
from typing import Any


FINAL_EXECUTIONS = re.compile(
    r"stat::number_of_executed_units:\s*([0-9]+)"
)
FINDING_MARKERS = (
    "ERROR: AddressSanitizer",
    "SUMMARY: AddressSanitizer",
    "SUMMARY: UndefinedBehaviorSanitizer",
    "runtime error:",
    "libFuzzer: timeout after",
    "deadly signal",
)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def command_output(command: list[str]) -> str:
    result = subprocess.run(
        command,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    return result.stdout.strip()


def run_target(
    *,
    source_root: Path,
    build_dir: Path,
    corpus_root: Path,
    report_root: Path,
    item: dict[str, Any],
    policy: dict[str, Any],
) -> dict[str, Any]:
    target_id = item["id"]
    binary = build_dir / "tests" / item["binary"]
    corpus = corpus_root / item["corpus"]
    artifact_dir = report_root / "artifacts" / target_id
    log_path = report_root / "logs" / f"{target_id}.log"
    artifact_dir.mkdir(parents=True, exist_ok=True)
    log_path.parent.mkdir(parents=True, exist_ok=True)

    if not binary.is_file():
        raise SystemExit(f"missing fuzz binary: {binary}")
    if not corpus.is_dir() or not any(corpus.iterdir()):
        raise SystemExit(f"empty fuzz corpus: {corpus}")

    command = [
        str(binary),
        str(corpus),
        f"-runs={policy['minimum_executions_per_target']}",
        f"-max_len={item['max_input_bytes']}",
        f"-timeout={policy['timeout_seconds']}",
        f"-rss_limit_mb={policy['rss_limit_mb']}",
        f"-malloc_limit_mb={policy['malloc_limit_mb']}",
        "-print_final_stats=1",
        f"-artifact_prefix={artifact_dir}{os.sep}",
    ]
    dictionary = item.get("dictionary")
    if dictionary:
        command.append(f"-dict={source_root / dictionary}")

    started = time.monotonic()
    result = subprocess.run(
        command,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
        env=os.environ.copy(),
    )
    elapsed = time.monotonic() - started
    output = result.stdout
    log_path.write_text(output, encoding="utf-8")

    match = FINAL_EXECUTIONS.search(output)
    executions = int(match.group(1)) if match else 0
    markers = [marker for marker in FINDING_MARKERS if marker in output]
    retained = sorted(
        path.name for path in artifact_dir.iterdir() if path.is_file()
    )

    passed = (
        result.returncode == 0
        and executions >= policy["minimum_executions_per_target"]
        and not markers
        and not retained
    )
    return {
        "id": target_id,
        "binary": item["binary"],
        "binary_sha256": sha256_file(binary),
        "command": command,
        "corpus": item["corpus"],
        "corpus_seed_count": len(
            [path for path in corpus.iterdir() if path.is_file()]
        ),
        "elapsed_seconds": round(elapsed, 6),
        "executions": executions,
        "finding_markers": markers,
        "libfuzzer_return_code": result.returncode,
        "log_path": str(log_path.relative_to(report_root)),
        "retained_crash_or_timeout_inputs": retained,
        "status": "pass" if passed else "fail",
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--corpus-root", type=Path, required=True)
    parser.add_argument("--campaign", type=Path, required=True)
    parser.add_argument("--source-revision", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    campaign = json.loads(args.campaign.read_text(encoding="utf-8"))
    policy = campaign["policy"]
    report_root = args.output.parent
    report_root.mkdir(parents=True, exist_ok=True)

    results = [
        run_target(
            source_root=args.source_root,
            build_dir=args.build_dir,
            corpus_root=args.corpus_root,
            report_root=report_root,
            item=item,
            policy=policy,
        )
        for item in campaign["targets"]
    ]
    aggregate = sum(item["executions"] for item in results)
    failed = [item["id"] for item in results if item["status"] != "pass"]
    passed = (
        not failed
        and aggregate >= policy["minimum_aggregate_executions"]
    )

    compiler = os.environ.get("CC", "clang")
    report = {
        "schema": "apta-fuzz-campaign-report-1",
        "campaign_version": campaign["campaign_version"],
        "campaign_policy_sha256": sha256_file(args.campaign),
        "compiler": {
            "command": compiler,
            "version": command_output([compiler, "--version"]),
        },
        "engine": campaign["engine"],
        "environment": {
            "asan_options": os.environ.get("ASAN_OPTIONS", ""),
            "ubsan_options": os.environ.get("UBSAN_OPTIONS", ""),
        },
        "minimum_aggregate_executions": policy[
            "minimum_aggregate_executions"
        ],
        "observed_aggregate_executions": aggregate,
        "platform": {
            "machine": platform.machine(),
            "python": platform.python_version(),
            "release": platform.release(),
            "system": platform.system(),
        },
        "policy": policy,
        "source_revision": args.source_revision,
        "status": "pass" if passed else "fail",
        "targets": results,
        "unresolved_findings": failed,
    }
    args.output.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        f"P7 fuzz campaign: {report['status']}; "
        f"targets={len(results)}; executions={aggregate}"
    )
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
