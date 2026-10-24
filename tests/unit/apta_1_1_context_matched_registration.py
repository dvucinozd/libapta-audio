#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""P3 schema/hash/public-counter arithmetic only; never import or run DSP/scorers."""
import ast
from copy import deepcopy
from fractions import Fraction
import hashlib
from itertools import product
import json
from pathlib import Path
import unittest

import jsonschema

ROOT = Path(__file__).resolve().parents[2]
DOCS = ROOT / 'docs/rust'
PROTOCOL = DOCS / 'CONTEXT-ANCHOR-MATCHED-EVALUATION-PROTOCOL.json'
LOAD = lambda path: json.loads(Path(path).read_text())
SHA = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
REG = LOAD(PROTOCOL)
REG_SHA = SHA(PROTOCOL)
SCHEMA = LOAD(DOCS / 'CONTEXT-ANCHOR-MATCHED-EVALUATION-SCHEMA.json')
EXAMPLES = LOAD(DOCS / 'CONTEXT-ANCHOR-MATCHED-EVALUATION-EXAMPLES.json')


def validate(record):
    try:
        jsonschema.Draft202012Validator(SCHEMA).validate(record)
    except jsonschema.ValidationError as error:
        raise ValueError('closed record shape differs') from error
    if record['protocol_sha256'] != REG_SHA:
        raise ValueError('registration identity differs')
    c = record['counts']
    if any(type(v) is not int for v in c.values()):
        raise ValueError('exact integer counters required')
    n, both = c['windows'], c['both_supported']
    if c['reference_complete_windows'] + c['reference_incomplete_windows'] != n:
        raise ValueError('reference support does not conserve')
    if sum(c[k] for k in ('both_supported', 'left_only_supported', 'right_only_supported', 'neither_supported')) != n:
        raise ValueError('prediction support does not partition windows')
    if sum(c[k] for k in ('correct_both', 'fixes_on_both', 'breaks_on_both', 'incorrect_both')) != both:
        raise ValueError('shared correctness does not conserve')
    if (c['left_correct_on_both'] != c['correct_both'] + c['breaks_on_both']
            or c['right_correct_on_both'] != c['correct_both'] + c['fixes_on_both']):
        raise ValueError('one-sided success mixed into paired correctness')
    if (c['left_only_correct'] > c['left_only_supported']
            or c['right_only_correct'] > c['right_only_supported']):
        raise ValueError('one-sided correctness exceeds its support')
    if record['level'] == 'source' and n > 8:
        raise ValueError('source window limit')
    if (record['stratum'] == 'reference_complete' and c['reference_incomplete_windows']
            or record['stratum'] == 'reference_incomplete' and c['reference_complete_windows']):
        raise ValueError('reference strata mixed')
    conflicts = c['legacy_native_unowned_meter_conflicts']
    if (conflicts > n - both or conflicts and (record['family'].startswith('grid_')
            or record['comparison'] == 'candidate_vs_original_context')):
        raise ValueError('legacy unowned conflict promoted or misassigned')
    if record['family'].startswith('grid_'):
        correct_union = sum(c[k] for k in ('correct_both', 'fixes_on_both', 'breaks_on_both',
                                          'left_only_correct', 'right_only_correct'))
        if correct_union > c['reference_complete_windows']:
            raise ValueError('grid correctness without complete reference context')
    for side in ('left', 'right'):
        rate = record[side + '_conditional_accuracy']
        if both == 0:
            if rate is not None:
                raise ValueError('empty shared denominator has no accuracy')
        elif (type(rate) is not dict or any(type(v) is not int for v in rate.values())
              or rate != {'numerator': c[side + '_correct_on_both'], 'denominator': both}):
            raise ValueError('conditional rate uses wrong domain')


def examples():
    result = deepcopy(EXAMPLES['examples'])
    for row in result:
        row['ledger']['protocol_sha256'] = REG_SHA
    return result


def count_rows(rows):
    """Independent finite truth-table accounting; no projection/scoring inputs."""
    c = {key: 0 for key in REG['count_keys']}
    for ls, rs, lc, rc, complete, conflict in rows:
        if any(type(v) is not int or v not in (0, 1) for v in (ls, rs, lc, rc, complete, conflict)):
            raise ValueError('binary public arithmetic required')
        if lc > ls or rc > rs:
            raise ValueError('unsupported side cannot be correct in matched channel')
        c['windows'] += 1
        c['reference_complete_windows'] += complete
        c['reference_incomplete_windows'] += 1 - complete
        c['legacy_native_unowned_meter_conflicts'] += conflict
        if ls and rs:
            c['both_supported'] += 1
            c['left_correct_on_both'] += lc
            c['right_correct_on_both'] += rc
            c['correct_both' if lc and rc else 'breaks_on_both' if lc else
              'fixes_on_both' if rc else 'incorrect_both'] += 1
        elif ls:
            c['left_only_supported'] += 1
            c['left_only_correct'] += lc
        elif rs:
            c['right_only_supported'] += 1
            c['right_only_correct'] += rc
        else:
            c['neither_supported'] += 1
    return c


def with_counts(template, counts):
    result = deepcopy(template)
    result['counts'] = counts
    for side in ('left', 'right'):
        result[side + '_conditional_accuracy'] = ({'numerator': counts[side + '_correct_on_both'],
            'denominator': counts['both_supported']} if counts['both_supported'] else None)
    return result


class MatchedRegistrationTests(unittest.TestCase):
    def test_bound_authorities_and_frozen_p0_p1_p2_c3_components(self):
        self.assertEqual(len(REG['binding_sha256']), 23)
        for name, digest in REG['binding_sha256'].items():
            self.assertEqual(SHA(ROOT / name), digest, name)
        p0 = LOAD(DOCS / 'CONTEXT-ANCHOR-PROJECTION-PROTOCOL.json')
        for name, digest in p0['binding'].items():
            self.assertEqual(SHA(DOCS / name), digest, name)
        for group in ('reviewed_source_sha256', 'frozen_c3_component_sha256'):
            for name, digest in p0[group].items():
                self.assertEqual(SHA(ROOT / name), digest, name)
        self.assertEqual(len(p0['frozen_c3_component_sha256']), 37)
        p2 = LOAD(DOCS / 'CONTEXT-ANCHOR-PROJECTION-P2-20261024.json')
        for name, digest in p2['component_sha256'].items():
            self.assertEqual(SHA(ROOT / name), digest, name)

    def test_schema_is_closed_with_local_references_only(self):
        jsonschema.Draft202012Validator.check_schema(SCHEMA)
        def inspect(value):
            if type(value) is dict:
                if '$ref' in value:
                    self.assertTrue(value['$ref'].startswith('#/$defs/'))
                if value.get('type') == 'object':
                    self.assertIs(value['additionalProperties'], False)
                for child in value.values():
                    inspect(child)
            elif type(value) is list:
                for child in value:
                    inspect(child)
        inspect(SCHEMA)

    def test_public_examples_independent_truth_table_counts(self):
        self.assertEqual(EXAMPLES['scope'], 'PUBLIC_COUNTER_ARITHMETIC_ONLY_NOT_MEASURED')
        self.assertFalse(EXAMPLES['projector_called'])
        self.assertFalse(EXAMPLES['scorer_called'])
        self.assertEqual(len(examples()), 3)
        for row in examples():
            validate(row['ledger'])
            self.assertEqual(count_rows(row['rows']), row['ledger']['counts'])

    def test_all_88_supported_correctness_and_context_truth_states(self):
        checked = 0
        for family in REG['families']:
            for ls, rs, lc, rc, complete in product(range(2), repeat=5):
                if lc > ls or rc > rs or family.startswith('grid_') and not complete and (lc or rc):
                    continue
                record = with_counts(examples()[0]['ledger'], count_rows([[ls, rs, lc, rc, complete, 0]]))
                record['family'] = family
                validate(record)
                checked += 1
        self.assertEqual(checked, 88)

    def test_empty_shared_domain_and_empty_stratum_do_not_invent_accuracy(self):
        record = examples()[1]['ledger']
        self.assertEqual(record['counts']['both_supported'], 0)
        self.assertIsNone(record['left_conditional_accuracy'])
        self.assertIsNone(record['right_conditional_accuracy'])
        for side in ('left', 'right'):
            bad = deepcopy(record)
            bad[side + '_conditional_accuracy'] = {'numerator': 0, 'denominator': 1}
            with self.assertRaises(ValueError):
                validate(bad)
        empty = with_counts(record, count_rows([]))
        for stratum in ('all', 'reference_complete', 'reference_incomplete'):
            empty['stratum'] = stratum
            validate(empty)

    def test_newly_supported_correct_cases_are_never_shared_fixes(self):
        record = examples()[0]['ledger']
        self.assertEqual(record['counts']['right_only_correct'], 2)
        self.assertEqual(record['counts']['fixes_on_both'], 1)
        bad = deepcopy(record)
        bad['counts']['fixes_on_both'] += bad['counts']['right_only_correct']
        with self.assertRaises(ValueError):
            validate(bad)
        conflict = examples()[2]['ledger']
        self.assertEqual(conflict['counts']['legacy_native_unowned_meter_conflicts'], 1)
        self.assertEqual(conflict['counts']['fixes_on_both'], 0)
        self.assertEqual(conflict['counts']['right_only_supported'], 1)

    def test_missing_extra_boolean_float_negative_and_foreign_identity_rejected(self):
        for change in (lambda v: v['counts'].pop('neither_supported'),
                       lambda v: v['counts'].update(windows=True),
                       lambda v: v['counts'].update(windows=8.0),
                       lambda v: v['counts'].update(windows=-1),
                       lambda v: v.update(extra_score=1),
                       lambda v: v.update(protocol_sha256='0' * 64),
                       lambda v: v.update(scope='REAL_GTZAN'),
                       lambda v: v.update(comparison='candidate_only')):
            bad = deepcopy(examples()[0]['ledger'])
            change(bad)
            with self.assertRaises(ValueError):
                validate(bad)

    def test_nonconservation_stratum_and_wrong_rate_denominator_rejected(self):
        changes = [lambda v, key=key: v['counts'].update({key: v['counts'][key] + 1})
                   for key in ('reference_complete_windows', 'both_supported', 'correct_both',
                               'left_correct_on_both', 'right_correct_on_both')]
        changes += [lambda v: v['left_conditional_accuracy'].update(denominator=8),
                    lambda v: v['right_conditional_accuracy'].update(numerator=3),
                    lambda v: v.update(stratum='reference_incomplete'),
                    lambda v: v['counts'].update(right_only_correct=3),
                    lambda v: v['counts'].update(legacy_native_unowned_meter_conflicts=1)]
        for change in changes:
            bad = deepcopy(examples()[0]['ledger'])
            change(bad)
            with self.assertRaises(ValueError):
                validate(bad)

    def test_scope_phase_boundaries_resource_caps_and_old_verdict_preserved(self):
        for key in ('execution_enabled', 'evaluator_implemented', 'runner_implemented', 'campaign_run',
                    'saved_cases_or_labels_opened', 'projector_called', 'scorer_called', 'candidate_reconstructed',
                    'candidate_axis_changed', 'corpus_run', 'model_inference_or_training', 'real_authorization_created',
                    'real_dispatch_enabled', 'native_activation', 'physical_p4', 'release'):
            self.assertIs(REG[key], False, key)
        scope = REG['future_generated_scope']
        self.assertEqual((scope['sources'], scope['windows'], scope['reference_complete_windows'],
                          scope['reference_incomplete_windows']), (100, 600, 400, 200))
        self.assertEqual(scope['new_context_projection_records'], scope['windows'] * scope['arms'])
        self.assertEqual(scope['evaluated_cells'], scope['new_context_projection_records'] * scope['representations'])
        self.assertTrue(scope['real_401_199_scope_excluded'])
        self.assertEqual(REG['phase_order'], ['PROJECT', 'VERIFY', 'EVALUATE'])
        self.assertEqual(REG['legacy_projector_allowed_only_in'], 'EVALUATE')
        self.assertEqual(REG['reference_labels_allowed_only_in'], 'EVALUATE')
        self.assertEqual(REG['runtime_limits'], {'source_seconds': 60, 'phase_seconds': 1800,
            'process_rss_kib': 262144, 'attempt_bytes_including_staged_inputs': 67108864,
            'maximum_events': 3072, 'maximum_windows': 8, 'maximum_source_frames': 1440000, 'sample_rate': 48000})
        self.assertEqual(REG['legacy_scientific_control']['verdict'], 'REJECTED')
        self.assertEqual(REG['legacy_scientific_control']['passed_gates'], 14)
        self.assertFalse(REG['legacy_scientific_control']['new_domain_may_relabel_old_gates'])
        parent = LOAD(DOCS / 'COUPLED-TIME-DEVELOPMENT-PROTOCOL.json')
        self.assertEqual(Fraction(REG['pair_screen']['event_comparison_absolute_epsilon']),
                         Fraction(str(parent['development_gates']['event_comparison_absolute_epsilon'])))
        self.assertFalse(REG['pair_screen']['musical_acceptance'])
        self.assertEqual(REG['stage']['next'], 'P4_PURE_MATCHED_LEDGER_METER_ADAPTER_AND_INDEPENDENT_ORACLE')

    def test_strict_json_integer_roundtrip_and_registration_import_boundary(self):
        def unique(items):
            result = {}
            for key, value in items:
                if key in result:
                    raise ValueError('duplicate JSON key')
                result[key] = value
            return result
        def nonfinite(value):
            raise ValueError('nonfinite JSON')
        for text in ('{"a":1,"a":2}', '{"a":NaN}', '{"a":Infinity}'):
            with self.assertRaises(ValueError):
                json.loads(text, object_pairs_hook=unique, parse_constant=nonfinite)
        value = 2**63 + 1
        self.assertEqual(json.loads(json.dumps({'coordinate': value}))['coordinate'], value)
        tree = ast.parse(Path(__file__).read_text())
        imports = [node.module if isinstance(node, ast.ImportFrom) else node.names[0].name
                   for node in ast.walk(tree) if isinstance(node, (ast.Import, ast.ImportFrom))]
        self.assertFalse(any(name.startswith('apta') for name in imports))


if __name__ == '__main__':
    unittest.main()
