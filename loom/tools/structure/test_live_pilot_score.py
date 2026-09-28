"""Scoring mechanics tests, not measured live model quality."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import unittest

try:
    from .live_pilot_score import score_case
except ImportError:
    from live_pilot_score import score_case

FIXTURES = Path(__file__).resolve().parents[2] / 'tests/fixtures/eval/live_structure_pilot_v1'


def cases(split='dev'):
    inputs = json.loads((FIXTURES / f'inputs.{split}.json').read_text())
    gold = {g['case_id']: g for g in json.loads((FIXTURES / f'gold.{split}.json').read_text())}
    return [(i, gold[i['case_id']]) for i in inputs]


def reference_bundle(case, gold):
    """Construct a response from expectations only to exercise scorer mechanics.

    This builder never analyzes text and must never be reported as an extractor.
    """
    packet = case['source_packet']
    text = packet['observations'][0]['text']
    full = {'observation': 'ob_source', 'byte_start': 0, 'byte_len': len(text.encode()), 'quote': text}
    bundle = {'schema': 'loom.candidate_graph/1', 'packet_id': packet['snapshot_id'],
              'entity_drafts': [], 'claim_drafts': [], 'roots': [], 'coverage': [], 'unknowns': []}
    if gold['expected'] == 'abstain':
        bundle['coverage'] = [{'support': [full], 'status': 'ambiguous',
                               'reason': 'Unresolved source reading.', 'drafts': []}]
        bundle['unknowns'] = [{'support': [full], 'reason': 'Unresolved source reading.'}]
        return bundle
    counter = 0
    contexts = {}

    def entity(kind, attrs, support=None):
        nonlocal counter
        counter += 1
        handle = f'@e{counter}'
        bundle['entity_drafts'].append({'handle': handle, 'kind': kind, 'label': 'unscored label',
                                       'attrs': attrs, 'support': deepcopy(support or [full])})
        return handle

    def claim(subject, pred, obj, scope, value=None, port=None, ordinal=0):
        nonlocal counter
        counter += 1
        extra = {'polarity': 'positive', 'assertion_context': contexts[scope]}
        if port is not None:
            extra.update(port=port, ordinal=ordinal)
        bundle['claim_drafts'].append({'handle': f'@c{counter}', 'subject': subject,
                                      'predicate': pred, 'object': obj, 'value': value,
                                      'qualifiers': {'scope': scope, 'extra': extra},
                                      'assessment': {'basis': {'support': [deepcopy(full)]},
                                                     'premises': {'claims': []}}})

    def scope(kind, context, parent=None):
        handle = entity('scope', {'scope_type': kind, 'assertion_context': context})
        contexts[handle] = context
        if parent:
            claim(handle, 'scope_parent', parent, handle)
        return handle

    def term(anchor, s, bindings, predicate=False):
        if isinstance(anchor, dict):
            idx = anchor['variable']
            symbol = f'v{idx}'
            handle = entity('term_occurrence', {'term_type': 'variable', 'symbol': symbol})
            claim(handle, 'bound_to', bindings[idx], s)
        else:
            handle = entity('term_occurrence', {'term_type': 'predicate' if predicate else 'constant',
                                                'symbol': 'not_used_for_scoring'}, gold['anchors'][anchor][:1])
        claim(handle, 'in_scope', s, s)
        return handle

    def walk(node, s, bindings):
        handle = entity('expression_occurrence', {})
        claim(handle, 'in_scope', s, s)
        op = node['op']
        claim(handle, 'operation_type', '', s, 'quantifier' if op in ('forall', 'exists') else op)
        ports = {}
        if op == 'predicate_application':
            ports['predicate'] = [term(node['predicate'], s, bindings, True)]
            ports['argument'] = [term(a, s, bindings) for a in node['arguments']]
        elif op == 'conditional':
            ports = {p: [walk(node[p], s, bindings)] for p in ('antecedent', 'consequent')}
        elif op == 'conjunction':
            ports['member'] = [walk(n, s, bindings) for n in node['members']]
        elif op == 'negation':
            ports['body'] = [walk(node['body'], s, bindings)]
        else:
            child = scope('quantifier', node['context'], s)
            binder = entity('binder', {'symbol': f'v{len(bindings)}'})
            claim(binder, 'in_scope', child, child)
            claim(handle, 'quantifier_kind', '', s, op)
            claim(handle, 'introduces_scope', child, s)
            ports['binder'] = [binder]
            ports['body'] = [walk(node['body'], child, bindings + [binder])]
        for port, values in ports.items():
            for ordinal, target in enumerate(values):
                claim(handle, 'operand', target, s, port=port, ordinal=ordinal)
        return handle

    for node in gold['roots']:
        s = scope('quotation' if node['context'] == 'quoted' else 'assertion', node['context'])
        bundle['roots'].append(walk(node, s, []))
    bundle['coverage'] = [{'support': [full], 'status': 'represented',
                           'reason': 'Fixture expectation rendered for mechanical tests.',
                           'drafts': bundle['roots'][:]}]
    return bundle


class LivePilotScoreTests(unittest.TestCase):
    def test_freeze_hashes_and_family_split(self):
        manifest = json.loads((FIXTURES / 'manifest.json').read_text())
        for name, wanted in manifest['files_sha256'].items():
            self.assertEqual(hashlib.sha256((FIXTURES / name).read_bytes()).hexdigest(), wanted)
        family_splits = {}
        for row in manifest['cases']:
            family_splits.setdefault(row['family'], set()).add(row['split'])
        self.assertEqual(len(family_splits), 16)
        self.assertTrue(all(len(v) == 1 for v in family_splits.values()))
        self.assertEqual(len(cases('dev')), 16)
        self.assertEqual(len(cases('validation')), 16)

    def test_all_32_reference_projections_validate_and_match(self):
        for case, gold in cases('dev') + cases('validation'):
            with self.subTest(case=case['case_id']):
                result = score_case(case, gold, reference_bundle(case, gold))
                self.assertTrue(result['contract_valid'], result['errors'])
                self.assertTrue(result['source_support_valid'])
                self.assertTrue(result['semantic_exact'], result)

    def test_argument_reversal_is_structurally_same_semantically_wrong(self):
        case, gold = cases()[0]
        bundle = reference_bundle(case, gold)
        args = [c for c in bundle['claim_drafts'] if c['predicate'] == 'operand' and
                c['qualifiers']['extra'].get('port') == 'argument']
        args[0]['object'], args[1]['object'] = args[1]['object'], args[0]['object']
        result = score_case(case, gold, bundle)
        self.assertTrue(result['contract_valid'])
        self.assertTrue(result['structural_exact'])
        self.assertFalse(result['semantic_exact'])

    def test_handles_labels_and_symbols_are_not_semantic_identity(self):
        case, gold = cases()[0]
        bundle = reference_bundle(case, gold)
        mapping = {r['handle']: '@renamed_' + str(i) for i, r in enumerate(bundle['entity_drafts'] + bundle['claim_drafts'])}
        def rename(value):
            if isinstance(value, dict):
                return {k: rename(v) for k, v in value.items()}
            if isinstance(value, list):
                return [rename(v) for v in value]
            return mapping.get(value, value) if isinstance(value, str) else value
        bundle = rename(bundle)
        for e in bundle['entity_drafts']:
            e['label'] = 'arbitrary paraphrase'
            if e['kind'] == 'term_occurrence':
                e['attrs']['symbol'] = 'different symbol'
        self.assertTrue(score_case(case, gold, bundle)['semantic_exact'])

    def test_direction_and_scope_errors_do_not_pass(self):
        for cid in ('case_003_en', 'case_005_pl', 'case_006_en'):
            case, gold = next((i, g) for i, g in cases() if i['case_id'] == cid)
            bundle = reference_bundle(case, gold)
            if cid == 'case_003_en':
                edges = [c for c in bundle['claim_drafts'] if c['predicate'] == 'operand' and
                         c['qualifiers']['extra'].get('port') in ('antecedent', 'consequent')]
                edges[0]['object'], edges[1]['object'] = edges[1]['object'], edges[0]['object']
            elif cid == 'case_005_pl':
                for e in bundle['entity_drafts']:
                    if e['kind'] == 'scope':
                        e['attrs'].update(scope_type='assertion', assertion_context='asserted')
                for c in bundle['claim_drafts']:
                    c['qualifiers']['extra']['assertion_context'] = 'asserted'
            else:
                next(c for c in bundle['claim_drafts'] if c['predicate'] == 'quantifier_kind')['value'] = 'exists'
            result = score_case(case, gold, bundle)
            self.assertTrue(result['contract_valid'], result['errors'])
            self.assertFalse(result['semantic_exact'], cid)

    def test_full_passage_term_support_cannot_fake_exact_roles(self):
        case, gold = cases()[0]
        bundle = reference_bundle(case, gold)
        full = bundle['coverage'][0]['support']
        for e in bundle['entity_drafts']:
            if e['kind'] == 'term_occurrence':
                e['support'] = deepcopy(full)
        result = score_case(case, gold, bundle)
        self.assertTrue(result['contract_valid'])
        self.assertTrue(result['source_support_valid'])
        self.assertTrue(result['structural_exact'])
        self.assertFalse(result['semantic_exact'])
        self.assertEqual(len(result['unresolved_anchors']), 3)

    def test_utf8_forgery_and_nonbundle_fail_without_false_grounding(self):
        case, gold = next((i, g) for i, g in cases() if i['case_id'] == 'case_002_pl')
        bundle = reference_bundle(case, gold)
        pred = next(e for e in bundle['entity_drafts'] if e['kind'] == 'term_occurrence' and e['attrs']['term_type'] == 'predicate')
        pred['support'][0]['quote'] = 'swieci'
        result = score_case(case, gold, bundle)
        self.assertFalse(result['source_support_valid'])
        self.assertFalse(result['contract_valid'])
        self.assertFalse(result['semantic_exact'])
        self.assertFalse(score_case(case, gold, None)['semantic_exact'])

    def test_abstention_is_located_and_does_not_count_as_represented(self):
        case, gold = next((i, g) for i, g in cases() if g['expected'] == 'abstain')
        bundle = reference_bundle(case, gold)
        result = score_case(case, gold, bundle)
        self.assertTrue(result['abstained'])
        self.assertFalse(result['represented'])
        self.assertTrue(result['semantic_exact'])
        bundle['unknowns'] = []
        self.assertFalse(score_case(case, gold, bundle)['semantic_exact'])

    def test_untrusted_input_is_bounded_before_deepcopy_or_hashing(self):
        case, gold = cases()[0]
        deep = []
        for _ in range(1000):
            deep = [deep]
        for value in (deep, {'bad': float('nan')}, {'bad': 1 << 4097}):
            result = score_case(case, gold, value)
            self.assertFalse(result['contract_valid'])
            self.assertFalse(result['semantic_exact'])
            self.assertIsNone(result['bundle_sha256'])
            self.assertEqual(result['errors'][0]['code'], 'score_input_bound')

    def test_case_and_source_identity_mismatch_is_caller_error(self):
        case, gold = cases()[0]
        altered = deepcopy(case)
        altered['source_packet']['observations'][0]['text'] += ' '
        with self.assertRaises(ValueError):
            score_case(altered, gold, reference_bundle(case, gold))


if __name__ == '__main__':
    unittest.main()
