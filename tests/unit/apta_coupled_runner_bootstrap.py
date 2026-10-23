#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""TEST ONLY: generated identities on the real CT04 phase machinery.

No tools/ module imports this file. Production has no settings/env bypass.
"""
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import apta_1_1_coupled_time as runner
import apta_dense_runner_support as io

settings_path = str(Path(sys.argv[1]).resolve())
cfg = json.loads(Path(settings_path).read_text())
runner.settings = lambda: cfg
runner.command = lambda *args: [sys.executable, str(Path(__file__).resolve()), settings_path, *map(str, args)]
original_evaluate = runner.evaluate


def generated_evaluate(*args, **kwargs):
    # Stand-in only for the dependency-free routine suite. Canonical mode uses
    # unchanged mir_eval in the retained experiment runtime, tested separately.
    if cfg['test_metric'] == 'stub':
        import apta_1_1_neural_comparison as comparison
        comparison.event_metrics = lambda truth, events: {
            key: float(events['beat_frames'] == truth['beat_frames'][:-1]) for key in runner.METRICS}
    return original_evaluate(*args, **kwargs)


runner.evaluate = generated_evaluate
original_supervise = io.supervise


def phase_supervise(command, seconds, rss_kib, byte_limit, *args, **kwargs):
    if '_phase' in command:
        limits = cfg.get('test_phase_limits', {})
        seconds = limits.get('max_phase_seconds', seconds)
        rss_kib = limits.get('max_process_rss_kib', rss_kib)
        byte_limit = limits.get('max_attempt_output_bytes', byte_limit)
    return original_supervise(command, seconds, rss_kib, byte_limit, *args, **kwargs)


io.supervise = phase_supervise
sys.argv = [sys.argv[0], *sys.argv[2:]]
try:
    sys.exit(runner.main())
except (ValueError, OSError, KeyError, TypeError, IndexError) as error:
    print(json.dumps({'status': 'INVALID', 'error': str(error)}), file=sys.stderr)
    sys.exit(2)
