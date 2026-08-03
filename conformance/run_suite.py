#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run the installed-package APTA public conformance suite."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import subprocess
import sys
from pathlib import Path
from typing import Any


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError(f"{path} must contain a JSON object")
    return value


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def profile_tests(
    manifest: dict[str, Any],
    profile_name: str,
    seen: set[str] | None = None,
) -> tuple[set[str], set[str]]:
    profiles = manifest["profiles"]
    if profile_name not in profiles:
        raise ValueError(f"unknown profile: {profile_name}")
    if seen is None:
        seen = set()
    if profile_name in seen:
        raise ValueError(f"profile inheritance cycle at {profile_name}")
    seen = set(seen)
    seen.add(profile_name)
    profile = profiles[profile_name]
    mandatory: set[str] = set(profile.get("mandatory", []))
    optional: set[str] = set(profile.get("optional", []))
    for parent in profile.get("extends", []):
        parent_mandatory, parent_optional = profile_tests(manifest, parent, seen)
        mandatory.update(parent_mandatory)
        optional.update(parent_optional)
    optional.difference_update(mandatory)
    return mandatory, optional


def normalize_output(value: str) -> str:
    return value.replace("\r\n", "\n").replace("\r", "\n").strip()




def verify_fixtures(manifest_path: Path, manifest: dict[str, Any]) -> None:
    base = manifest_path.parent
    for fixture in manifest.get("fixtures", []):
        path = base / fixture["path"]
        text = "".join(path.read_text(encoding="ascii").split())
        try:
            data = bytes.fromhex(text)
        except ValueError as error:
            raise ValueError(f"invalid hex fixture {path}: {error}") from error
        if len(data) != fixture["size_bytes"]:
            raise ValueError(
                f"fixture size mismatch for {fixture['name']}: "
                f"{len(data)} != {fixture['size_bytes']}"
            )
        digest = hashlib.sha256(data).hexdigest()
        if digest != fixture["sha256"]:
            raise ValueError(
                f"fixture hash mismatch for {fixture['name']}: "
                f"{digest} != {fixture['sha256']}"
            )


def parse_key_value(values: list[str]) -> dict[str, str]:
    output: dict[str, str] = {}
    for value in values:
        if "=" not in value:
            raise ValueError(f"expected key=value, got {value!r}")
        key, item = value.split("=", 1)
        if not key:
            raise ValueError("empty key")
        output[key] = item
    return dict(sorted(output.items()))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--bin-dir", type=Path, required=True)
    parser.add_argument("--exe-suffix", default=".exe" if os.name == "nt" else "")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--runtime-dir", type=Path)
    parser.add_argument("--implementation-name", required=True)
    parser.add_argument("--implementation-version", required=True)
    parser.add_argument("--source-revision", default=os.environ.get("GITHUB_SHA", "unknown"))
    parser.add_argument("--profile", action="append", default=[])
    parser.add_argument("--qualifier", action="append", default=[])
    parser.add_argument("--platform-system", default=platform.system())
    parser.add_argument("--platform-processor", default=platform.machine())
    parser.add_argument("--platform-toolchain", required=True)
    parser.add_argument("--build-option", action="append", default=[])
    parser.add_argument("--deviation", action="append", default=[])
    parser.add_argument("--platform-exception", action="append", default=[])
    parser.add_argument("--evidence", action="append", default=[])
    parser.add_argument("--timeout-seconds", type=int, default=180)
    args = parser.parse_args()

    manifest = load_json(args.manifest)
    fixtures = load_json(args.fixtures)
    if manifest.get("manifest_version") != "apta-public-conformance-manifest-1":
        raise ValueError("unsupported conformance manifest version")
    if fixtures.get("manifest_version") != "apta-public-fixture-manifest-1":
        raise ValueError("unsupported fixture manifest version")
    verify_fixtures(args.fixtures, fixtures)

    claimed_profiles = args.profile or ["WAVEFORM-1.0"]
    claimed_qualifiers = args.qualifier
    mandatory: set[str] = set()
    optional: set[str] = set()
    profile_requirements: dict[str, set[str]] = {}
    for profile_name in claimed_profiles:
        required, profile_optional = profile_tests(manifest, profile_name)
        profile_requirements[profile_name] = required
        mandatory.update(required)
        optional.update(profile_optional)
    qualifier_requirements: dict[str, set[str]] = {}
    for qualifier_name in claimed_qualifiers:
        qualifier = manifest["qualifiers"].get(qualifier_name)
        if qualifier is None:
            raise ValueError(f"unknown qualifier: {qualifier_name}")
        required = set(qualifier.get("mandatory", []))
        qualifier_requirements[qualifier_name] = required
        mandatory.update(required)
        optional.update(qualifier.get("optional", []))
    optional.difference_update(mandatory)

    tests_by_id = {case["id"]: case for case in manifest["tests"]}
    unknown = (mandatory | optional) - set(tests_by_id)
    if unknown:
        raise ValueError(f"manifest references unknown tests: {sorted(unknown)}")

    results: list[dict[str, Any]] = []
    result_by_id: dict[str, str] = {}
    current_platform = args.platform_system.lower()
    selected = mandatory | optional
    for case in manifest["tests"]:
        case_id = case["id"]
        if case_id not in selected:
            continue
        exemptions = {str(item).lower() for item in case.get("platform_exempt", [])}
        required = case_id in mandatory
        if current_platform in exemptions:
            status = "skip"
            detail = "platform-exempt"
            stdout = ""
            stderr = ""
            return_code: int | None = None
        else:
            executable = args.bin_dir / f"{case['executable']}{args.exe_suffix}"
            if not executable.exists():
                status = "skip"
                detail = "executable-not-built"
                stdout = ""
                stderr = ""
                return_code = None
            else:
                environment = os.environ.copy()
                if args.runtime_dir is not None:
                    if os.name == "nt":
                        variable = "PATH"
                    elif sys.platform == "darwin":
                        variable = "DYLD_LIBRARY_PATH"
                    else:
                        variable = "LD_LIBRARY_PATH"
                    previous = environment.get(variable, "")
                    environment[variable] = str(args.runtime_dir)
                    if previous:
                        environment[variable] += os.pathsep + previous
                completed = subprocess.run(
                    [str(executable)],
                    capture_output=True,
                    text=True,
                    timeout=args.timeout_seconds,
                    check=False,
                    env=environment,
                )
                return_code = completed.returncode
                stdout = normalize_output(completed.stdout)
                stderr = normalize_output(completed.stderr)
                status = "pass" if completed.returncode == 0 else "fail"
                detail = ""
        result_by_id[case_id] = status
        row: dict[str, Any] = {
            "id": case_id,
            "classification": case["classification"],
            "required": required,
            "status": status,
        }
        if return_code is not None:
            row["return_code"] = return_code
        if detail:
            row["detail"] = detail
        if stdout:
            row["stdout"] = stdout
        if stderr:
            row["stderr"] = stderr
        results.append(row)

    profile_results: dict[str, str] = {}
    for name, required in profile_requirements.items():
        profile_results[name] = (
            "pass"
            if all(result_by_id.get(case_id) == "pass" for case_id in required)
            else "fail"
        )
    qualifier_results: dict[str, str] = {}
    for name, required in qualifier_requirements.items():
        qualifier_results[name] = (
            "pass"
            if all(result_by_id.get(case_id) == "pass" for case_id in required)
            else "fail"
        )

    counts = {
        "pass": sum(row["status"] == "pass" for row in results),
        "fail": sum(row["status"] == "fail" for row in results),
        "skip": sum(row["status"] == "skip" for row in results),
    }
    mandatory_valid = all(
        result_by_id.get(case_id) == "pass" for case_id in mandatory
    )
    report = {
        "schema_version": "apta-conformance-report-1",
        "implementation": {
            "name": args.implementation_name,
            "version": args.implementation_version,
            "source_revision": args.source_revision or "unknown",
        },
        "claim": {
            "profiles": claimed_profiles,
            "qualifiers": claimed_qualifiers,
            "profile_results": profile_results,
            "qualifier_results": qualifier_results,
        },
        "platform": {
            "system": args.platform_system,
            "processor": args.platform_processor,
            "toolchain": args.platform_toolchain,
            "build_options": parse_key_value(args.build_option),
        },
        "suite": {
            "version": manifest["suite_version"],
            "manifest_version": manifest["manifest_version"],
            "manifest_sha256": sha256(args.manifest),
            "fixture_manifest_version": fixtures["manifest_version"],
            "fixture_manifest_sha256": sha256(args.fixtures),
        },
        "counts": counts,
        "tests": results,
        "deviations": sorted(args.deviation),
        "platform_exceptions": sorted(args.platform_exception),
        "evidence": parse_key_value(args.evidence),
        "overall_status": "pass" if mandatory_valid else "fail",
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    print(
        f"APTA conformance: {report['overall_status']} "
        f"(pass={counts['pass']} fail={counts['fail']} skip={counts['skip']})"
    )
    return 0 if mandatory_valid else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"conformance runner error: {error}", file=sys.stderr)
        raise SystemExit(2)
