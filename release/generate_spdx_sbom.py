#!/usr/bin/env python3
"""Generate a deterministic SPDX 2.3 JSON document for a release directory."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import re
from pathlib import Path


def digest(path: Path, algorithm: str) -> str:
    hasher = hashlib.new(algorithm)
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def spdx_id_for_path(relative: str) -> str:
    token = re.sub(r"[^A-Za-z0-9.-]+", "-", relative).strip("-")
    suffix = hashlib.sha1(relative.encode("utf-8")).hexdigest()[:12]
    token = token[:60] or "file"
    return f"SPDXRef-File-{token}-{suffix}"


def iso_time(epoch: int) -> str:
    return (
        dt.datetime.fromtimestamp(epoch, tz=dt.timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z")
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--source-revision", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--created-epoch", type=int)
    args = parser.parse_args()

    root = args.root.resolve()
    output = args.output.resolve()
    if not root.is_dir():
        raise SystemExit(f"release root is not a directory: {root}")

    epoch = args.created_epoch
    if epoch is None:
        epoch = int(os.environ.get("SOURCE_DATE_EPOCH", "0"))
    if epoch < 0:
        raise SystemExit("created epoch must be non-negative")

    rows = []
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.resolve() == output:
            continue
        relative = path.relative_to(root).as_posix()
        sha1 = digest(path, "sha1")
        sha256 = digest(path, "sha256")
        rows.append((relative, sha1, sha256))

    if not rows:
        raise SystemExit("release root contains no files")

    verification_input = "".join(sorted(row[1] for row in rows)).encode("ascii")
    verification_code = hashlib.sha1(verification_input).hexdigest()

    package_id = "SPDXRef-Package-libapta-audio"
    namespace_revision = re.sub(r"[^A-Za-z0-9.-]", "-", args.source_revision)
    document = {
        "SPDXID": "SPDXRef-DOCUMENT",
        "spdxVersion": "SPDX-2.3",
        "dataLicense": "CC0-1.0",
        "name": f"{args.name}-{args.version}",
        "documentNamespace": (
            "https://spdx.org/spdxdocs/"
            f"{args.name}-{args.version}-{namespace_revision}"
        ),
        "creationInfo": {
            "created": iso_time(epoch),
            "creators": ["Tool: libapta-audio/release/generate_spdx_sbom.py"],
        },
        "documentDescribes": [package_id],
        "packages": [
            {
                "SPDXID": package_id,
                "name": args.name,
                "versionInfo": args.version,
                "downloadLocation": "NOASSERTION",
                "filesAnalyzed": True,
                "licenseConcluded": "NOASSERTION",
                "licenseDeclared": "Apache-2.0",
                "copyrightText": "NOASSERTION",
                "externalRefs": [
                    {
                        "referenceCategory": "OTHER",
                        "referenceType": "source-revision",
                        "referenceLocator": args.source_revision,
                    }
                ],
                "packageVerificationCode": {
                    "packageVerificationCodeValue": verification_code
                },
            }
        ],
        "files": [],
        "relationships": [],
    }

    for relative, sha1, sha256 in rows:
        file_id = spdx_id_for_path(relative)
        document["files"].append(
            {
                "SPDXID": file_id,
                "fileName": f"./{relative}",
                "checksums": [
                    {"algorithm": "SHA1", "checksumValue": sha1},
                    {"algorithm": "SHA256", "checksumValue": sha256},
                ],
                "licenseConcluded": "NOASSERTION",
                "copyrightText": "NOASSERTION",
            }
        )
        document["relationships"].append(
            {
                "spdxElementId": package_id,
                "relationshipType": "CONTAINS",
                "relatedSpdxElement": file_id,
            }
        )

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(document, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
