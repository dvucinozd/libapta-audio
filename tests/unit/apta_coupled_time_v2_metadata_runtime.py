#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Fixed metadata test/campaign launcher; no arbitrary command or real-input CLI."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests/unit')]
import apta_1_1_coupled_time_v2_metadata as runner
import apta_coupled_time_v2_runtime as frozen
import apta_dense_runner_support as io

SUITES = {
    'canonical': (('canonical', 'apta_1_1_reference_support.py', ()),
                  ('canonical', 'apta_1_1_coupled_time_evaluation_v2_metadata.py', ()),
                  ('canonical', 'apta_1_1_coupled_time.py', ()),
                  ('canonical', 'apta_1_1_coupled_time_verify.py', ())),
    'contracts': (('system', 'apta_1_1_coupled_time_metadata_registration.py', ()),
                  ('system', 'apta_1_1_coupled_time_v2_metadata_runner.py', ('RuntimeTests',))),
    'process': (('canonical', 'apta_1_1_coupled_time_v2_metadata_runner.py', ('ProcessTests',)),),
    'preparation': (('canonical', 'apta_1_1_coupled_time_v2_metadata_preflight.py', ()),),
}


def checked_snapshot(snapshot):
    snapshot = runner.safe_path(snapshot)
    op = runner.registered_settings()['operational']
    if io.sha(snapshot / 'snapshot.json') != op['runtime_snapshot_sha256']:
        raise ValueError('registered immutable metadata runtime required')
    return frozen.verify(snapshot)


def commands(snapshot, receipt, suite):
    """Build only fixed suite commands; never expose a user-chosen executable."""
    return [frozen.sandbox_command(snapshot, [receipt['interpreter'] if runtime == 'canonical' else '/usr/bin/python3',
            ROOT / 'tests/unit' / script, *selectors, '-v'], receipt)
            for runtime, script, selectors in SUITES[suite]]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('role', choices=('check', 'probe', 'campaign'))
    parser.add_argument('--snapshot', required=True, type=Path)
    parser.add_argument('--suite', choices=tuple(SUITES))
    parser.add_argument('--output', type=Path)
    parser.add_argument('--source-fixture', type=Path)
    args = parser.parse_args()
    if (args.role == 'check') != (args.suite is not None):
        parser.error('--suite is required only for check')
    if args.role == 'campaign':
        if args.output is None or args.source_fixture is None:
            parser.error('campaign requires new output and source-fixture destinations')
    elif args.output is not None or args.source_fixture is not None:
        parser.error('destinations are allowed only for campaign')
    receipt = checked_snapshot(args.snapshot)
    if args.role == 'check':
        selected = commands(args.snapshot, receipt, args.suite)
    elif args.role == 'probe':
        selected = [frozen.sandbox_command(args.snapshot, [receipt['interpreter'], frozen.__file__, '_probe',
                                                          '--snapshot', args.snapshot], receipt)]
    else:
        selected = [frozen.sandbox_command(args.snapshot, [receipt['interpreter'],
                    ROOT / 'tests/unit/apta_coupled_time_v2_metadata_preflight.py',
                    '--runtime-snapshot', args.snapshot, '--output', args.output,
                    '--source-fixture', args.source_fixture], receipt)]
    for command in selected:
        result = subprocess.run(command, cwd=ROOT)
        checked_snapshot(args.snapshot)
        if result.returncode:
            raise ValueError('metadata child failed; no automatic campaign retry')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError, TypeError) as error:
        print(json.dumps({'status': 'INVALID', 'error': str(error)}), file=sys.stderr)
        sys.exit(2)
