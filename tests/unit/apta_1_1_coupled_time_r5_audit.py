#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generated tests for saved-artifact diagnosis, never candidate/scorer repair."""
import ast
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import apta_coupled_time_r5_audit as audit


class AuditTests(unittest.TestCase):
    def events(self, beats):
        return {'beat_frames': beats, 'downbeat_frames': [beats[i] for i in (0, 4, 8)]}

    def fixture(self):
        before = self.events([100010 + 12800 * i for i in range(9)])
        after = self.events([100001] + [99997 + 12800 * i for i in range(1, 9)])
        window = {'first_frame': 100000, 'end_frame': 230000}
        project = lambda events: audit.reference.project(events['beat_frames'], events['downbeat_frames'], **{
            'first': window['first_frame'], 'end': window['end_frame']})
        return before, after, window, project(before), project(after)

    def test_boundary_anchor_rejection_preserves_owned_events_and_topology(self):
        before, after, window, old, new = self.fixture()
        result = audit.inspect_case(before, after, window, old, new)
        self.assertTrue(result['same_source_topology'])
        self.assertTrue(result['same_window_event_membership'])
        self.assertEqual(result['old']['guard'], 'ACCEPTED')
        self.assertTrue(result['old']['meter_present'])
        self.assertEqual(result['candidate']['guard'], 'ANCHOR_BEFORE_WINDOW')
        self.assertTrue(result['candidate']['tempo_in_registered_range'])
        self.assertEqual(result['candidate']['anchor_offset_from_window_start'], -1)
        self.assertFalse(result['candidate']['meter_present'])

    def test_saved_projection_must_match_unchanged_canonical_projector(self):
        before, after, window, old, new = self.fixture()
        new['selected'] = {'kind': 'accepted'}
        with self.assertRaisesRegex(ValueError, 'exactly replay'):
            audit.inspect_case(before, after, window, old, new)

    def test_insufficient_projection_does_not_invent_fit_or_meter(self):
        events = {'beat_frames': [100010, 112810], 'downbeat_frames': [100010]}
        window = {'first_frame': 100000, 'end_frame': 130000}
        saved = audit.reference.project(events['beat_frames'], events['downbeat_frames'], 100000, 130000)
        result = audit.projection_trace(events, window, saved)
        self.assertEqual(result['guard'], 'INSUFFICIENT_EVENTS')
        self.assertNotIn('rounded_anchor_frame', result)

    def test_missing_counter_is_not_zero_unsupported_context(self):
        result = audit.support_summary({'windows': 600, 'reference_complete_windows': 400})
        self.assertEqual(result['reference_context_not_complete'], 200)
        self.assertIsNone(result['serialized_incomplete_counter'])
        self.assertEqual(result['missing_counter_lookup_default'], 0)
        self.assertFalse(result['incomplete_counter_emitted'])
        valid = audit.support_summary({'windows': 600, 'reference_complete_windows': 400, 'reference_incomplete_windows': 200})
        self.assertTrue(valid['incomplete_counter_emitted'])
        for value in ({'windows': True, 'reference_complete_windows': 0},
                      {'windows': 3, 'reference_complete_windows': 4},
                      {'windows': 600, 'reference_complete_windows': 400, 'reference_incomplete_windows': 0}):
            with self.assertRaises(ValueError):
                audit.support_summary(value)

    def test_static_emitter_audit_distinguishes_absent_field(self):
        missing = 'def score_points():\n return {"windows": 1, "reference_complete_windows": 0}\n'
        present = 'def score_points():\n return {"windows": 1, "reference_complete_windows": 0, "reference_incomplete_windows": 1}\n'
        self.assertNotIn('reference_incomplete_windows', audit.emitted_keys(missing))
        self.assertIn('reference_incomplete_windows', audit.emitted_keys(present))

    def test_topology_rejects_duplicate_events_and_detached_downbeat(self):
        for events in ({'beat_frames': [1, 1], 'downbeat_frames': [1]},
                       {'beat_frames': [1, 2], 'downbeat_frames': [3]},
                       {'beat_frames': [True, 2], 'downbeat_frames': []}):
            with self.assertRaises(ValueError):
                audit.topology(events)

    def test_artifact_boundary_hash_and_label_guards(self):
        with tempfile.TemporaryDirectory(prefix='libapta-r5-test-') as temporary:
            base = Path(temporary)
            saved = base / 'saved'
            saved.mkdir()
            path = saved / 'record.json'
            path.write_text('{"value":1}')
            digest = audit.io.sha(path)
            self.assertEqual(audit.read_saved(path, digest, {}, saved), {'value': 1})
            for invalid in (base / 'outside.json', saved / 'labels.json'):
                with self.assertRaisesRegex(ValueError, 'labels forbidden'):
                    audit.read_saved(invalid, digest, {}, saved)
            link = saved / 'link.json'
            link.symlink_to(path)
            with self.assertRaisesRegex(ValueError, 'symlink'):
                audit.read_saved(link, digest, {}, saved)
            path.write_text('{"value":2}')
            with self.assertRaises(ValueError):
                audit.read_saved(path, digest, {}, saved)
            marker = saved / 'marker'
            marker.write_bytes(b'preserve')
            with self.assertRaisesRegex(ValueError, 'disjoint'):
                audit.run(saved)
            self.assertEqual(marker.read_bytes(), b'preserve')

    def test_cli_cannot_select_audio_labels_inputs_counts_or_limits(self):
        with tempfile.TemporaryDirectory(prefix='libapta-r5-cli-') as temporary:
            output = Path(temporary) / 'absent'
            for option in ('--inputs', '--audio', '--labels', '--count', '--timeout', '--run'):
                result = subprocess.run([sys.executable, str(Path(audit.__file__)), '--output', str(output), option, 'unused'],
                                        capture_output=True, text=True, timeout=15)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('unrecognized arguments', result.stderr)
            self.assertFalse(output.exists())

    def test_auditor_has_no_constructor_or_scoring_calls(self):
        tree = ast.parse(Path(audit.__file__).read_text())
        forbidden = {'construct', 'verify_bytes', 'score_source', 'score_window', 'event_metrics'}
        calls = {node.func.attr for node in ast.walk(tree) if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute)}
        self.assertFalse(calls & forbidden)


if __name__ == '__main__':
    unittest.main()
