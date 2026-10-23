#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Neutral file binding and Linux supervision; no corpus/scoring imports."""
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time


def sha(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def bind(path, expected, bindings):
    path = Path(path)
    if not path.is_file() or path.is_symlink() or sha(path) != expected:
        raise ValueError("bound artifact identity differs")
    bindings[str(path.resolve())] = expected


def recheck(bindings):
    for name, digest in bindings.items():
        bind(Path(name), digest, {})


def write_new(path, value):
    # Same exclusive, canonical JSON bytes as history.write_new, without importing
    # its corpus-validation dependency into construction.
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2, sort_keys=True, allow_nan=False)
        stream.write("\n")


def json_bytes(value):
    return len((json.dumps(value, indent=2, sort_keys=True, allow_nan=False)+"\n").encode("utf-8"))


def read_bound(path, expected, bindings):
    bind(path, expected, bindings)
    value = json.loads(Path(path).read_text())
    bind(path, expected, bindings)
    return value


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args], text=True).strip()


def check_source(root, revision):
    if git(root, "rev-parse", "HEAD") != revision or git(root, "status", "--porcelain"):
        raise ValueError("measurement source is not the clean frozen revision")


def source_snapshot(root, bindings):
    revision = git(root, "rev-parse", "HEAD")
    check_source(root, revision)
    names = subprocess.check_output(["git", "-C", str(root), "ls-files", "-z"]).decode().split("\0")
    for name in filter(None, names):
        path = Path(root) / name
        bind(path, sha(path), bindings)
    return revision


def runtime_snapshot(bindings, expected=None):
    """Bind loaded source/cache/extensions, interpreter and loaded shared libs."""
    files = {Path(sys.executable).resolve()}
    for module in list(sys.modules.values()):
        for attr in ("__file__", "__cached__"):
            name = getattr(module, attr, None)
            if name:
                path = Path(name).resolve()
                if path.is_file():
                    files.add(path)
    maps = Path("/proc/self/maps")
    if maps.exists():
        for line in maps.read_text().splitlines():
            name = line.split()[-1]
            if name.startswith("/") and ".so" in name and Path(name).is_file():
                files.add(Path(name).resolve())
    for path in sorted(files):
        digest = sha(path)
        if expected is not None and str(path) in expected and expected[str(path)] != digest:
            raise ValueError("legacy-bound runtime changed")
        bind(path, digest, bindings)
    return len(files)


def runtime_recheck(bindings, expected=None):
    loaded = {}
    runtime_snapshot(loaded, expected)
    if any(bindings.get(path) != digest for path,digest in loaded.items()):
        raise ValueError("runtime file was not bound before phase execution")
    recheck(loaded)


def external(path, root):
    if Path(path).resolve().is_relative_to(Path(root).resolve()):
        raise ValueError("private output must remain outside the checkout")


def disjoint_output(output, inputs):
    output = Path(output).resolve()
    if output.exists():
        raise ValueError("output already exists")
    for item in inputs:
        item = Path(item).resolve()
        if output.is_relative_to(item) or item.is_relative_to(output):
            raise ValueError("output overlaps input evidence")


def tree_bytes(roots):
    files = set()
    total = 0
    for root in roots:
        root = Path(root)
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if path.is_symlink():
                raise ValueError("symlink in attempt output")
            if path.is_file():
                name = path.resolve()
                if name not in files:
                    total += path.stat().st_size
                    files.add(name)
    return total


def process_peaks(pid, group):
    """RSS/HWM for the root and all members of its inherited process group."""
    values = {}
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        current = int(entry.name)
        try:
            if current not in (pid, os.getpid()) and (not group or os.getpgid(current) != pid):
                continue
            text = (entry / "status").read_text()
            metrics = {}
            for line in text.splitlines():
                if line.startswith(("VmRSS:", "VmHWM:")):
                    key, amount, unit = line.split()
                    if unit != "kB":
                        raise ValueError("unexpected proc RSS unit")
                    metrics[key] = int(amount)
            if metrics:
                values[str(current)] = max(metrics.values())
        except (FileNotFoundError, ProcessLookupError, PermissionError):
            continue
    return values


def supervise(command, seconds, rss_kib, byte_limit, output_roots, log_prefix, group=True):
    """Kill on time/RSS/output violation; preserve logs, raise instead of PASS."""
    if sys.platform != "linux" or seconds <= 0 or rss_kib <= 0 or byte_limit <= 0:
        raise ValueError("positive limits and Linux procfs required")
    started = time.monotonic()
    prefix = Path(log_prefix)
    prefix.parent.mkdir(parents=True, exist_ok=True)
    peaks, proc, failure = {}, None, None
    with Path(str(prefix) + ".stdout").open("xb") as stdout, Path(str(prefix) + ".stderr").open("xb") as stderr:
        try:
            proc = subprocess.Popen(command, stdout=stdout, stderr=stderr, start_new_session=group)
            while True:
                for pid, value in process_peaks(proc.pid, group).items():
                    peaks[pid] = max(peaks.get(pid, 0), value)
                if max(peaks.values(), default=0) > rss_kib:
                    raise ValueError("RSS cap exceeded")
                if time.monotonic() - started > seconds:
                    raise ValueError("wall-time cap exceeded")
                if tree_bytes(output_roots) > byte_limit:
                    raise ValueError("attempt output cap exceeded")
                status = proc.poll()
                if status is not None:
                    if status:
                        raise ValueError("worker returned failure")
                    # A phase must not leave a live descendant after successful exit.
                    if group and any(int(p) not in (proc.pid, os.getpid()) for p in process_peaks(proc.pid, True)):
                        raise ValueError("orphan phase descendant")
                    break
                time.sleep(.02)
        except BaseException as exc:
            failure = str(exc)
            if proc is not None:
                if group:
                    try:
                        os.killpg(proc.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                elif proc.poll() is None:
                    proc.kill()
                proc.wait()
            raise
        finally:
            if proc is not None:
                result = {"status": "INVALID" if failure else "PASS", "failure": failure,
                          "wall_seconds": time.monotonic() - started, "root_pid": proc.pid,
                          "peak_rss_kib_by_pid": peaks,
                          "max_observed_process_rss_kib": max(peaks.values(), default=0),
                          "supervisor_pid": os.getpid(),
                          "poll_seconds": .02, "process_group_monitored": group}
                if not failure and tree_bytes(output_roots)+json_bytes(result) > byte_limit:
                    result.update(status="INVALID",failure="final supervisor output cap exceeded")
                write_new(Path(str(prefix) + ".resources.json"), result)
    if tree_bytes(output_roots) > byte_limit:
        raise ValueError("final supervisor output cap exceeded")
    return result
