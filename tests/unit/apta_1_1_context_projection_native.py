#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Opt-in P2 public generated native/consumer contracts; never score saved cases.

Prepare external tools: python3 THIS_FILE --prepare BUILD_DIR CONSUMER_REPO EVIDENCE_DIR
Run: APTA_P2_BUILD=BUILD_DIR APTA_P2_OUTPUT=NEW_OUTPUT_DIR python3 THIS_FILE -v
The external build step preserves real manifests, locks, sources and checkouts.
"""
from collections import Counter
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import apta_context_projection as projection
import apta_context_projection_oracle as oracle
import apta_1_1_context_projection_registration as registration
import apta_dense_runner_support as io

SNAPSHOT = 'df7f8f4aea3852cafb660ea67dae26f1610287ec'
COUNTS = Counter()
MAX_CALL_SECONDS = 0.0


def binding_check():
    protocol = json.loads((ROOT / 'docs/rust/CONTEXT-ANCHOR-PROJECTION-P2-PROTOCOL.json').read_text())
    for name, digest in protocol['binding_sha256'].items():
        if io.sha(ROOT / name) != digest:
            raise ValueError('P2 bound authority changed: ' + name)
    for section in ('reviewed_source_sha256', 'frozen_c3_component_sha256'):
        for name, digest in registration.REG[section].items():
            if io.sha(ROOT / name) != digest:
                raise ValueError('frozen component changed: ' + name)
    for name, digest in registration.REG['binding'].items():
        if io.sha(ROOT / 'docs/rust' / name) != digest:
            raise ValueError('frozen document changed: ' + name)


def prepare(build, consumer, evidence):
    binding_check()
    probe_files = ('tools/probes/context_projection_native.c', 'rust/examples/context_projection_probe.rs',
                   'tools/probes/context_projection_consumer.rs', 'tests/unit/apta_1_1_context_projection_native.py')
    component_sha256 = {name: io.sha(ROOT / name) for name in probe_files}
    build, consumer, evidence = (Path(p).resolve() for p in (build, consumer, evidence))
    io.external(build, ROOT)
    io.external(evidence, ROOT)
    if build == evidence or build in evidence.parents or evidence in build.parents:
        raise ValueError('separate build and evidence directories required')
    build.mkdir(parents=True, exist_ok=False)
    evidence.mkdir(parents=True, exist_ok=False)
    cargo = str(Path.home() / '.cargo/bin/cargo')
    resources = {}

    def run(name, command):
        # Compiler scratch files are renamed/unlinked while building. Monitor
        # stable logs live; measure the build tree after each completed phase.
        resources[name] = io.supervise(command, 600, 1048576, 67108864,
                                       [evidence], evidence / name)
        if io.tree_bytes([build]) > 536870912:
            raise ValueError('completed build tree exceeds compile artifact cap')

    run('configure-c', ['cmake', '-S', str(ROOT), '-B', str(build / 'c'),
        '-DAPTA_BUILD_TESTS=OFF', '-DAPTA_BUILD_TOOLS=OFF', '-DAPTA_BUILD_DESKTOP_ADAPTERS=OFF',
        '-DAPTA_WARNINGS_AS_ERRORS=ON', '-DCMAKE_BUILD_TYPE=Debug'])
    run('build-c', ['cmake', '--build', str(build / 'c'), '--target', 'apta_core', '-j2'])
    run('compile-c-probe', ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
        '-I', str(ROOT / 'include'), '-I', str(ROOT / 'src/core'),
        str(ROOT / 'tools/probes/context_projection_native.c'), str(build / 'c/libapta.a'),
        '-lm', '-o', str(build / 'c-probe')])
    run('build-rust', [cargo, 'build', '--manifest-path', str(ROOT / 'rust/Cargo.toml'),
        '--locked', '--offline', '--example', 'context_projection_probe', '--target-dir', str(build / 'rust')])

    # Extract exact public source bytes from the registered Git object, never the
    # potentially older/dirty consumer checkout. Minimal manifests are probe-only.
    crates = ('ruzina-apta-adapter', 'ruzina-track-analysis', 'ruzina-waveform')
    copied = {}
    for crate in crates:
        prefix = 'firmware-rust/crates/' + crate + '/src/'
        names = subprocess.check_output(['git', '-C', str(consumer), 'ls-tree', '-r', '--name-only',
                                         SNAPSHOT, prefix], text=True).splitlines()
        if not names:
            raise ValueError('missing registered consumer source')
        for name in names:
            data = subprocess.check_output(['git', '-C', str(consumer), 'show', SNAPSHOT + ':' + name])
            target = build / 'consumer' / crate / 'src' / name[len(prefix):]
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            copied[name] = hashlib.sha256(data).hexdigest()
    expected = registration.REG['consumer_snapshot']['files']['firmware-rust/crates/ruzina-apta-adapter/src/lib.rs']['sha256']
    if copied['firmware-rust/crates/ruzina-apta-adapter/src/lib.rs'] != expected:
        raise ValueError('consumer source snapshot differs from P0')
    workspace = build / 'consumer'
    (workspace / 'Cargo.toml').write_text('[workspace]\nresolver="3"\nmembers=["probe",' +
                                         ','.join(json.dumps(c) for c in crates) + ']\n')
    common = 'version="0.1.0"\nedition="2024"\nrust-version="1.95.0"\npublish=false\n'
    libapta = '{path=' + json.dumps(str(ROOT / 'rust')) + ',default-features=false}'
    for crate in crates:
        text = '[package]\nname=' + json.dumps(crate) + '\n' + common
        if crate == 'ruzina-apta-adapter':
            text += '[features]\ndefault=[]\nworkers=[]\nowned-handoff=[]\nlibrary-deck-load=[]\n'
            text += '[dependencies]\nlibapta=' + libapta + '\nruzina-track-analysis={path="../ruzina-track-analysis"}\nruzina-waveform={path="../ruzina-waveform"}\n'
        elif crate == 'ruzina-track-analysis':
            text += '[features]\ndefault=[]\nowned=[]\n'
        else:
            text += '[dependencies]\nruzina-track-analysis={path="../ruzina-track-analysis"}\nembedded-graphics={version="=0.8.2",default-features=false}\n'
        (workspace / crate / 'Cargo.toml').write_text(text)
    (workspace / 'probe/src').mkdir(parents=True)
    (workspace / 'probe/src/main.rs').write_bytes((ROOT / 'tools/probes/context_projection_consumer.rs').read_bytes())
    (workspace / 'probe/Cargo.toml').write_text('[package]\nname="context-projection-consumer"\n' + common +
        '[dependencies]\nlibapta=' + libapta + '\nruzina-apta-adapter={path="../ruzina-apta-adapter"}\nruzina-track-analysis={path="../ruzina-track-analysis"}\n')
    run('lock-consumer', [cargo, 'generate-lockfile', '--offline', '--manifest-path', str(workspace / 'Cargo.toml')])
    run('build-consumer', [cargo, 'build', '--locked', '--offline', '--manifest-path', str(workspace / 'Cargo.toml'),
        '--target-dir', str(build / 'consumer-target'), '-p', 'context-projection-consumer'])
    binding_check()
    if component_sha256 != {name: io.sha(ROOT / name) for name in probe_files}:
        raise ValueError('probe source changed during build')
    executables = {'c': build / 'c-probe', 'rust': build / 'rust/debug/examples/context_projection_probe',
                   'consumer': build / 'consumer-target/debug/context-projection-consumer'}
    io.write_new(evidence / 'build.json', {'resources': resources, 'component_sha256': component_sha256,
        'consumer_snapshot': SNAPSHOT,
        'consumer_source_sha256': copied, 'consumer_checkout_revision_preserved': io.git(consumer, 'rev-parse', 'HEAD'),
        'probe_manifests_only_dependency_substituted': True,
        'external_consumer_lock_sha256': io.sha(workspace / 'Cargo.lock'),
        'executables': {k: {'path': str(p), 'sha256': io.sha(p)} for k, p in executables.items()},
        'external_build_bytes': io.tree_bytes([build]), 'build_logs_bytes_before_receipt': io.tree_bytes([evidence])})


def generated_cases():
    def case(beats, db, first=0, total=None, end=None):
        total = total or beats[-1] + 10000
        return dict(beat_frames=beats, downbeat_ordinals=db, total_frames=total,
                    first_frame=first, end_frame=end or total)
    result = {'constant4': case([1000 + 24000 * i for i in range(12)], [0, 4, 8], total=288000),
              'context4': registration.examples()[1]['input'],
              'fractional3': case([1000 + 23001 * i + i % 2 for i in range(17)], [0, 3, 6, 9, 12, 15]),
              'no_meter': case([1000 + 24000 * i for i in range(12)], [])}
    for right in (0, 8100):
        offsets = [0] * 20
        offsets[0], offsets[3], offsets[6] = 18, 8100, right
        offsets[19] = -(3 * 8100 + 6 * right - sum(offsets)) // 18
        offsets[1] = -sum(offsets)
        result['right_witness' if right == 0 else 'no_owned_witness'] = case(
            [24000 * i + d for i, d in enumerate(offsets)], [0, 3, 6, 9, 12, 15], first=18, total=480000)
    for i in range(8):
        result['window' + str(i)] = case([1000 + 12000 * j for j in range(120)], list(range(0, 120, 4)),
                                         first=180000 * i, end=180000 * (i + 1), total=1440000)
    return result


class NativeProjectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        binding_check()
        cls.build = Path(os.environ['APTA_P2_BUILD']).resolve()
        cls.output = Path(os.environ['APTA_P2_OUTPUT']).resolve()
        io.external(cls.output, ROOT)
        cls.output.mkdir(parents=True, exist_ok=False)
        cls.executables = {'c': cls.build / 'c-probe', 'rust': cls.build / 'rust/debug/examples/context_projection_probe',
                           'consumer': cls.build / 'consumer-target/debug/context-projection-consumer'}
        cls.outputs = {}
        for name, value in generated_cases().items():
            before = deepcopy(value)
            actual = projection.project_window(**value)
            if actual != oracle.independent_project_window(**value) or value != before or actual['status'] != 'ACCEPTED':
                raise ValueError('P1 public oracle disagreement')
            registration.validate_example(value, actual)
            cls.outputs[name] = actual
        cls.records = {}

    def call(self, tool, *args):
        global MAX_CALL_SECONDS
        started = time.monotonic()
        process = subprocess.run([str(self.executables[tool]), *map(str, args)],
                                 capture_output=True, text=True, timeout=60)
        MAX_CALL_SECONDS = max(MAX_CALL_SECONDS, time.monotonic() - started)
        self.assertEqual(process.returncode, 0, process.stderr)
        self.assertEqual(process.stderr, '')
        COUNTS['process_calls'] += 1
        return json.loads(process.stdout)

    def fixture(self, name, representation, defect=0):
        p = self.outputs[name]
        m, model = p['meter'], p['model']
        fields = [p['total_frames'], p['first_frame'], p['end_frame'], model['anchor_frame'],
                  model['anchor_ordinal'], model['tempo_millibpm'], model['period_q32'] >> 32,
                  model['period_q32'] & (projection.Q - 1), p['point_count'], int(m is not None),
                  m['numerator'] if m else 0, m['downbeat_frame'] if m else 0,
                  m['downbeat_ordinal'] if m else 0, representation, defect]
        path = self.output / (name + '-' + str(representation) + '-' + str(defect) + '.txt')
        with path.open('x') as stream:
            stream.write(' '.join(map(str, fields)) + '\n')
            for b in p['points']:
                stream.write(f"{b['position_q32'] >> 32} {b['position_q32'] & (projection.Q - 1)} {b['ordinal']}\n")
        return path

    def test_01_cross_writer_reader_and_target_payload_parity(self):
        for name, p in self.outputs.items():
            expected = {'range': [p['first_frame'], p['end_frame']],
                        'points': [[b['position_q32'] >> 32, b['position_q32'] & (projection.Q - 1), b['ordinal']]
                                   for b in p['points']], 'meter': None}
            if p['meter']:
                m = p['meter']
                expected['meter'] = [m['numerator'], 4, m['downbeat_frame'], m['downbeat_ordinal']]
            for representation in (1, 2, 3):
                fixture = self.fixture(name, representation)
                digest = io.sha(fixture)
                paths = []
                for writer in ('c', 'rust'):
                    path = fixture.with_suffix('.' + writer + '.apta')
                    self.assertEqual(self.call(writer, 'build', fixture, path), expected)
                    paths.append(path)
                    COUNTS['builder_acceptances'] += 1
                    for reader in ('c', 'rust'):
                        self.assertEqual(self.call(reader, 'read', path), expected)
                        COUNTS['reader_roundtrips'] += 1
                payloads = [self.call('rust', 'payload', path) for path in paths]
                self.assertEqual(payloads[0], payloads[1], (name, representation))
                self.assertEqual(set(payloads[0]), {'GGRD', 'REVN', 'MTRD'} if p['meter'] else {'GGRD', 'REVN'})
                COUNTS['exact_target_payload_comparisons'] += 1
                self.assertEqual(io.sha(fixture), digest)
                self.records[name, representation] = paths[1]
        self.assertTrue(self.outputs['context4']['meter']['downbeat_fraction_q32'])
        self.assertTrue(self.outputs['fractional3']['model']['period_q32'] & (projection.Q - 1))

    def test_02_invalid_meter_and_hybrid_coordinates_rejected(self):
        for representation, defect in [(r, d) for r in (1, 2, 3) for d in (1, 2, 3)] + [(3, 4)]:
            fixture = self.fixture('context4', representation, defect)
            for writer in ('c', 'rust'):
                path = fixture.with_suffix('.' + writer + '.apta')
                self.assertTrue(self.call(writer, 'build', fixture, path)['rejected'])
                self.assertFalse(path.exists())
                COUNTS['negative_builder_rejections'] += 1

    def test_03_actual_snapshot_policy_separate_from_native_validity(self):
        for (name, representation), path in self.records.items():
            p = self.outputs[name]
            actual = self.call('consumer', path, 'normal')
            self.assertTrue(actual['native_valid'])
            if not p['meter']:
                expected = 'tempo_only'
            elif representation == 3 or p['first_frame'] != 0 or p['end_frame'] != p['total_frames']:
                expected = 'UnsupportedGrid'
            else:
                expected = 'grid'
            self.assertEqual(actual['adapter'], expected, (name, representation))
            COUNTS['consumer_' + expected] += 1
            if expected != 'grid':
                self.assertTrue(actual['destination_unchanged'])
            else:
                t, q = p['model']['tempo_millibpm'], projection.Q
                self.assertEqual(actual['beats'], [[(b['position_q32'] * 1000 + 24000 * q) // (48000 * q),
                    (b['ordinal'] - p['meter']['downbeat_ordinal']) % p['meter']['numerator'], (t + 5) // 10]
                    for b in p['points']])
        path = self.records['constant4', 1]
        for mode in ('missing_meter', 'provisional_meter', 'provisional_grid', 'capacity'):
            actual = self.call('consumer', path, mode)
            self.assertTrue(actual['native_valid'])
            self.assertEqual(actual['adapter'], 'Capacity' if mode == 'capacity' else 'tempo_only')
            self.assertTrue(actual['destination_unchanged'])
            COUNTS['consumer_extra_' + mode] += 1

    def test_04_unsupported_inputs_never_enter_native_builders(self):
        for row in registration.examples()[2:]:
            p = projection.project_window(**row['input'])
            self.assertEqual(p['status'], row['output']['status'])
            self.assertIsNone(p['model'])
            COUNTS['explicit_unsupported_not_dispatched'] += 1
        binding_check()


if __name__ == '__main__':
    if len(sys.argv) == 5 and sys.argv[1] == '--prepare':
        prepare(*sys.argv[2:])
    else:
        suite = unittest.main(exit=False)
        print(json.dumps({'tests_run': suite.result.testsRun, 'failures': len(suite.result.failures),
            'errors': len(suite.result.errors), 'skips': len(suite.result.skipped),
            'counts': dict(COUNTS), 'max_probe_seconds': MAX_CALL_SECONDS}, sort_keys=True))
        sys.exit(not suite.result.wasSuccessful())
