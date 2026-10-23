#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""TEST ONLY: inject generated identities into the unchanged CLI phase machinery.

Never imported by tools/. No production override flag or environment hook exists.
Source cleanliness is checked in a disposable generated Git repository, with the
actual four implementation files additionally bound by their current hashes.
"""
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import apta_1_1_dense_decoder as runner
import apta_dense_decoder_verify as verifier
import apta_dense_runner_support as io

settings_path, role = sys.argv[1:3]
settings = json.loads(Path(settings_path).read_text())
runner.protocol = lambda: settings["protocol"]
runner.PREFLIGHT = Path(settings["preflight"])
snapshot, clean, supervise = io.source_snapshot, io.check_source, io.supervise


def source_snapshot(_root, bindings):
    revision = snapshot(Path(settings["source"]), bindings)
    for name in settings["components"]:
        io.bind(ROOT / name, io.sha(ROOT / name), bindings)
    return revision


def check_source(_root, revision):
    return clean(Path(settings["source"]), revision)


def supervised(command, *args, **kwargs):
    if command[1] in (str(Path(runner.__file__).resolve()), str(Path(verifier.__file__).resolve())):
        worker_role = "runner" if command[1] == str(Path(runner.__file__).resolve()) else "verifier"
        command = [command[0], str(Path(__file__).resolve()), settings_path, worker_role, *command[2:]]
    return supervise(command, *args, **kwargs)


io.source_snapshot, io.check_source, io.supervise = source_snapshot, check_source, supervised
sys.argv = [sys.argv[0], *sys.argv[3:]]
sys.exit(runner.main() if role == "runner" else verifier.main())
