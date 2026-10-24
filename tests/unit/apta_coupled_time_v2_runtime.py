#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Test-only immutable runtime for fixed generated checks and the R4 driver."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import sysconfig
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import apta_1_1_coupled_time_v2 as runner
import apta_dense_runner_support as io

FORMAT = 'libapta-generated-runtime-snapshot-1'
SUITES = {
    'runtime': ['tests/unit/apta_1_1_coupled_time_v2_runtime.py'],
    'v2': ['tests/unit/apta_1_1_coupled_time_v2_runner.py'],
    'driver': ['tests/unit/apta_1_1_coupled_time_v2_preflight.py'],
    'numerical': ['tests/unit/apta_1_1_coupled_time.py', 'tests/unit/apta_1_1_coupled_time_verify.py'],
}


def write(path, value):
    io.write_new(path, value)


def inventory(paths):
    """Link text is an identity too; never follow directory links while copying."""
    result = []
    for root in paths:
        entries = [root]
        if root.is_dir():
            entries += sorted(root.rglob('*'))
        for path in entries:
            mode = path.lstat().st_mode
            if stat.S_ISLNK(mode):
                row = {'kind': 'symlink', 'target': os.readlink(path)}
            elif stat.S_ISREG(mode):
                row = {'kind': 'file', 'bytes': path.stat().st_size, 'sha256': io.sha(path)}
            elif stat.S_ISDIR(mode):
                row = {'kind': 'directory'}
            else:
                raise ValueError('unsupported runtime entry')
            result.append({'path': str(path), **row})
    return result


def canonical_inventory(rows):
    return hashlib.sha256(json.dumps(rows, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def roots():
    # Run prepare with the retained experiment interpreter. Freeze complete trees,
    # including lazy imports, wheel libraries and bytecode, not just current maps.
    expected = json.loads((ROOT / 'docs/rust/COUPLED-TIME-RUNNER-V2-R3-20261024.json').read_text())['runtime']
    if (sys.prefix == sys.base_prefix or sys.version.split()[0] != expected['python']
            or io.sha(Path(sys.executable).resolve()) != expected['canonical_interpreter_sha256']):
        raise ValueError('retained canonical experiment interpreter required')
    candidates = [Path(sys.prefix), Path(sysconfig.get_path('stdlib')),
                  Path(sys.base_prefix) / 'lib' / sysconfig.get_config_var('MULTIARCH'),
                  Path(sys.executable).resolve(), Path('/usr/bin/git'), Path('/usr/bin/bwrap'),
                  Path(subprocess.check_output(['git', '--exec-path'], text=True).strip()),
                  Path('/usr/share/git-core'), Path(sys.base_prefix) / 'lib/python3',
                  Path('/usr/local/lib/python3.12'),
                  Path('/etc/python%d.%d' % sys.version_info[:2]),
                  Path('/etc/alternatives'), Path('/etc/ld.so.cache')]
    return sorted(set(path for path in candidates if path.exists()))


def create(destination, selected=None):
    started = time.monotonic()
    destination = runner.safe_path(destination)
    io.external(destination, ROOT)
    if destination.exists():
        raise ValueError('new exclusive runtime snapshot required')
    selected = roots() if selected is None else sorted(map(Path, selected))
    if (not selected or any(not path.is_absolute() or path.is_symlink() for path in selected)
            or any(a != b and (a.is_relative_to(b) or b.is_relative_to(a)) for a in selected for b in selected)
            or any(destination.is_relative_to(path) or path.is_relative_to(destination) for path in selected)
            or any(ROOT.is_relative_to(path) or path.is_relative_to(ROOT) for path in selected)):
        raise ValueError('runtime roots must be disjoint regular external paths')
    before = inventory(selected)
    destination.mkdir(parents=True)
    try:
        payload = destination / 'tree'
        for path in selected:
            target = payload / str(path).lstrip('/')
            target.parent.mkdir(parents=True, exist_ok=True)
            if path.is_dir():
                shutil.copytree(path, target, symlinks=True, copy_function=shutil.copy2)
            else:
                shutil.copy2(path, target)
        # Both the copied bytes and the still-live host bytes must match the
        # original complete inventory. A concurrent package transaction fails.
        copied = inventory([payload / str(path).lstrip('/') for path in selected])
        for row in copied:
            row['path'] = '/' + str(Path(row['path']).relative_to(payload))
        if before != copied or before != inventory(selected):
            raise ValueError('runtime changed during snapshot creation')
        write(destination / 'inventory.json', before)
        receipt = {'format': FORMAT, 'status': 'SNAPSHOT_READY', 'generated_only': True,
                   'roots': list(map(str, selected)), 'interpreter': str(Path(sys.executable)),
                   'inventory_sha256': io.sha(destination / 'inventory.json'),
                   'inventory_identity': canonical_inventory(before),
                   'regular_files': sum(row['kind'] == 'file' for row in before),
                   'regular_bytes': sum(row.get('bytes', 0) for row in before),
                   'create_seconds': time.monotonic() - started,
                   'component_sha256': io.sha(__file__)}
        write(destination / 'snapshot.json', receipt)
        return receipt
    except BaseException as error:
        write(destination / 'INVALID.json', {'status': 'INVALID', 'error': str(error)})
        raise


def verify(destination):
    destination = runner.safe_path(destination)
    if (destination / 'INVALID.json').exists():
        raise ValueError('closed invalid runtime snapshot')
    receipt = runner.strict_json((destination / 'snapshot.json').read_bytes())
    if (receipt['format'] != FORMAT or receipt['status'] != 'SNAPSHOT_READY'
            or receipt['generated_only'] is not True or receipt['component_sha256'] != io.sha(__file__)):
        raise ValueError('stale runtime snapshot')
    rows = runner.strict_json((destination / 'inventory.json').read_bytes())
    if (io.sha(destination / 'inventory.json') != receipt['inventory_sha256']
            or canonical_inventory(rows) != receipt['inventory_identity']):
        raise ValueError('runtime inventory identity differs')
    selected = [runner.safe_path(path) for path in receipt['roots']]
    if any(path.is_relative_to(destination) or destination.is_relative_to(path) for path in selected):
        raise ValueError('runtime snapshot overlaps mount destination')
    payload = destination / 'tree'
    # Do not resolve absolute symlinks against the host while inspecting copies.
    actual = inventory([payload / str(path).lstrip('/') for path in selected])
    for row in actual:
        row['path'] = '/' + str(Path(row['path']).relative_to(payload))
    if actual != rows:
        raise ValueError('runtime snapshot bytes or links changed')
    return receipt


def sandbox_command(destination, arguments, receipt=None):
    destination = runner.safe_path(destination)
    receipt = verify(destination) if receipt is None else receipt
    # The outer filesystem remains writable for generated fixtures and the Git
    # source topology. Each copied runtime root is replaced by a read-only bind.
    # This isolates runtime bytes; it is not a private-data access sandbox.
    command = ['bwrap', '--die-with-parent', '--new-session', '--bind', '/', '/',
               '--proc', '/proc', '--dev', '/dev', '--ro-bind', str(destination), str(destination)]
    for name in receipt['roots']:
        path = runner.safe_path(name)
        command += ['--ro-bind', str(destination / 'tree' / str(path).lstrip('/')), str(path)]
    command += ['--chdir', str(ROOT), '--', *map(str, arguments)]
    return command


def probe(destination):
    """Executed inside the namespace; require loaded runtime in copied RO roots."""
    import importlib.metadata
    import numpy
    import scipy
    import mir_eval
    import apta_1_1_coupled_time
    receipt = json.loads((Path(destination) / 'snapshot.json').read_text())
    runtime = {}
    io.runtime_snapshot(runtime)
    packages = {name: importlib.metadata.version(name) for name in ('numpy', 'scipy', 'mir_eval')}
    expected = json.loads((ROOT / 'docs/rust/COUPLED-TIME-RUNNER-V2-R3-20261024.json').read_text())['runtime']
    if packages != expected['packages'] or sys.version.split()[0] != expected['python']:
        raise ValueError('canonical metric runtime versions differ')
    frozen = [Path(path) for path in receipt['roots']]
    mount_rows = []
    for line in Path('/proc/self/mountinfo').read_text().splitlines():
        left, right = line.split(' - ', 1)
        fields = left.split()
        mount_rows.append((Path(fields[4].replace('\\040', ' ')), fields[5].split(',')))
    for path in frozen:
        mounts = [flags for point, flags in mount_rows if point == path]
        if not mounts or 'ro' not in mounts[-1]:
            raise ValueError('runtime root is not mounted read-only')
    unexpected = [path for path in runtime if not Path(path).is_relative_to(ROOT)
                  and not any(Path(path) == root or Path(path).is_relative_to(root) for root in frozen)]
    if unexpected:
        raise ValueError('loaded runtime escapes frozen roots: ' + Path(unexpected[0]).name)
    manifest = json.loads((Path(destination) / 'inventory.json').read_text())
    expected_files = {row['path']: row['sha256'] for row in manifest if row['kind'] == 'file'}
    for name, digest in runtime.items():
        if not Path(name).is_relative_to(ROOT) and expected_files.get(name) != digest:
            raise ValueError('loaded runtime differs from snapshot inventory')
    return {'status': 'FROZEN_RUNTIME_PROBE_PASS', 'generated_only': True,
            'python': sys.version.split()[0], 'packages': packages, 'runtime': runtime,
            'read_only_roots': len(frozen), 'snapshot_sha256': io.sha(Path(destination) / 'snapshot.json')}


def execute(destination, arguments, canonical=False):
    receipt = verify(destination)
    env = os.environ.copy()
    if canonical:
        env['APTA_CT04_CANONICAL_EVENTS'] = '1'
    result = subprocess.run(sandbox_command(destination, arguments, receipt), cwd=ROOT, env=env)
    verify(destination)
    if result.returncode:
        raise ValueError('frozen runtime child failed; no automatic retry')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('role', choices=('prepare', 'verify', 'probe', 'check', '_probe', 'campaign'))
    parser.add_argument('--snapshot', type=Path, required=True)
    parser.add_argument('--suite', choices=tuple(SUITES))
    parser.add_argument('--output', type=Path)
    parser.add_argument('--source-fixture', type=Path)
    args = parser.parse_args()
    if args.role != 'check' and args.suite is not None or args.role == 'check' and args.suite is None:
        parser.error('--suite required only for check')
    if args.role == 'campaign':
        if args.output is None or args.source_fixture is None:
            parser.error('campaign needs new output and source-fixture destinations')
    elif args.output is not None or args.source_fixture is not None:
        parser.error('campaign destinations are forbidden for this role')
    if args.role == 'prepare':
        print(json.dumps(create(args.snapshot), sort_keys=True))
    elif args.role == 'verify':
        print(json.dumps(verify(args.snapshot), sort_keys=True))
    elif args.role == '_probe':
        print(json.dumps(probe(args.snapshot), sort_keys=True))
    elif args.role == 'probe':
        receipt = verify(args.snapshot)
        execute(args.snapshot, [receipt['interpreter'], __file__, '_probe', '--snapshot', args.snapshot])
    elif args.role == 'campaign':
        receipt = verify(args.snapshot)
        execute(args.snapshot, [receipt['interpreter'], Path(__file__).with_name('apta_coupled_time_v2_preflight.py'),
                               '--runtime-snapshot', args.snapshot, '--output', args.output,
                               '--source-fixture', args.source_fixture])
    else:
        receipt = verify(args.snapshot)
        # Canonical v2 fixture and independent Draft202012 library both run in
        # this interpreter. No production argument/environment hook is added.
        for script in SUITES[args.suite]:
            execute(args.snapshot, [receipt['interpreter'], ROOT / script, '-v'], canonical=args.suite == 'v2')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError, TypeError) as error:
        print(json.dumps({'status': 'INVALID', 'error': str(error)}), file=sys.stderr)
        sys.exit(2)
