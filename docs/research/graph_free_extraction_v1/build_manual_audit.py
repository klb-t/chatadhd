"""Reproduce explicit post-outcome manual labels; does not repair or score outputs."""
from collections import Counter
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from loom.tools.structure import graph_free_extraction as free, graph_panel_live as panel

HERE = Path(__file__).resolve().parent
# These are manually assigned source diagnoses, not learned rules or new primary
# labels. Every other duplicate-free raw assertion was reviewed source-valid.
INVALID = {
    ('gpv1_dev_004', 'a2'): 'reporter_nonendorsement_is_not_positive_endorsement',
    ('gpv1_dev_006', 'a2'): 'reporter_nonendorsement_is_not_new_quoted_speaker_assertion_at_t2',
    ('gpv1_dev_007', 'a1'): 'affirmed_implies_between_signed_nodes_not_negative_prevents',
    ('gpv1_dev_008', 'a1'): 'operand_negation_does_not_make_affirmed_implication_negative',
    ('gpv1_dev_009', 'a1'): 'affirmed_implies_not_prevents_negative_target',
    ('gpv1_dev_010', 'a1'): 'affirmed_implies_not_prevents_negative_target',
    ('gpv1_dev_010', 'a2'): 'silence_is_not_denial_and_endpoints_not_declared_own_node_ids',
    ('gpv1_dev_011', 'a1'): 'affirmed_implies_not_prevents_negative_target',
    ('gpv1_dev_012', 'a1'): 'affirmed_implies_not_prevents_negative_target',
    ('gpv1_dev_012', 'a2'): 'no_explicit_support_self_edge_for_silence_statement',
}
NEEDS = {('gpv1_dev_007', 'a2'): 'unsupported_denies_predicate_plus_negative_polarity_intended_operator_ambiguous'}
EXPRESSION_NODES = {
    'gpv1_dev_001': {'n3', 'n4', 'n5'}, 'gpv1_dev_003': {'n3', 'n4'},
    'gpv1_dev_004': {'n3', 'n4', 'n5'}, 'gpv1_dev_005': {'n3', 'n4'},
    'gpv1_dev_006': {'n3', 'n4', 'n5', 'n6'}, 'gpv1_dev_010': {'n3', 'n4'},
    'gpv1_dev_012': {'n3', 'n4'}, 'gpv1_dev_014': {'n4'},
}


def citations(item, case):
    turns = {t['id']: t for t in case['turns']}
    ids = [v if isinstance(v, str) else v.get('turn_id') for v in item['evidence']]
    return [{**turns[i], 'source_id': case['source_id'],
             'known_at_matches_record': turns[i]['known_at'] == item['known_at']} for i in ids]


def build():
    source = json.loads((HERE / 'raw_contract_diagnostics.json').read_text())
    cases = {c['id']: c for c in panel.load_dev_inputs()}
    golds = {g['id']: g for g in panel.load_dev_gold()}
    rows = []; node_counts = Counter(); edge_counts = Counter(); event_counts = Counter()
    excluded = []
    for raw in source['cases']:
        cid = raw['case_id']; case = cases[cid]
        if raw['object'] is None:
            excluded.append({'case_id': cid, 'reason': 'paid_length_truncation_no_complete_json_graph',
                             'response_sha256': raw['response_sha256']})
            continue
        value = raw['object']; mapping, details = free.align_nodes(value['nodes'], case['node_inventory'])
        used = {e[k] for e in golds[cid]['source_assertions'] for k in ('source', 'target')}
        own = {n['id']: n['text'] for n in value['nodes']}; nodes = []
        for d in details:
            if d['reference_node_id'] in used:
                category = 'used_reference_atom'
                reason = 'exact_fixed_reference_text_or_predeclared_alias;not_truth_of_atom'
            elif d['reference_node_id'] is not None:
                category = 'unused_reference_control'
                reason = 'source_mentions_proposition_without_asserting_it_true;not_gold_edge_endpoint'
            elif d['own_node_id'] in EXPRESSION_NODES.get(cid, set()):
                category = 'source_supported_expression_outside_reference'
                reason = 'reviewed_reporting_conditional_denial_or_nonendorsement_expression;not_operand_atom'
            else:
                raise ValueError('unreviewed_node_requires_manual_label')
            node_counts[category] += 1
            nodes.append({**d, 'manual_source_category': category, 'manual_reason': reason,
                          'content_truth': 'unverified'})
        assertions = []
        for edge in value['source_assertions']:
            key = (cid, edge['id'])
            duplicate_fields = [d for d in raw['duplicate_fields'] if d['path'].startswith('$.source_assertions') and
                d['competing_values'] == edge.get('source', {}).get('duplicate_values', [])] if isinstance(edge.get('source'), dict) else []
            if isinstance(edge.get('source'), dict) and 'duplicate_values' in edge['source']:
                classification, reason = 'needs_review', 'duplicate_endpoint_fields_retained_no_last_value_selection'
            elif key in INVALID:
                classification, reason = 'source_invalid', INVALID[key]
            elif key in NEEDS:
                classification, reason = 'needs_review', NEEDS[key]
            else:
                classification, reason = 'source_valid', 'manually_reviewed_exact_typed_relation_actor_polarity_direction_and_dated_source'
            edge_counts[classification] += 1
            source_id, target_id = edge.get('source'), edge.get('target')
            own_source = own.get(source_id) if isinstance(source_id, str) else None
            own_target = own.get(target_id) if isinstance(target_id, str) else None
            evidence = citations(edge, case)
            if not all(e['known_at_matches_record'] for e in evidence):
                raise ValueError('citation_time_review_drift')
            assertions.append({'raw_record': edge, 'own_source_text': own_source, 'own_target_text': own_target,
                'manual_source_class': classification, 'manual_reason': reason,
                'exact_source_turns': evidence, 'primary_contract_status_unchanged': True,
                'duplicate_field_diagnostics': duplicate_fields})
        events = []
        for event in value['status_events']:
            # All six reviewed correction events point to explicit negative old
            # edge records: legitimate denial-target convention, unlike primary
            # gold positive-replacement convention. Keep that difference explicit.
            old = next(e for e in value['source_assertions'] if e['id'] == event['assertion_id'])
            new = next(e for e in value['source_assertions'] if e['id'] == event['superseded_by'])
            assert old['polarity'] == 'positive' and new['polarity'] == 'negative'
            assert old['attributed_to'] == new['attributed_to'] == 'owner'
            assert old['source'] == new['source'] and old['target'] == new['target']
            event_counts['source_valid_under_alternative_denial_target_convention'] += 1
            events.append({'raw_record': event,
                'manual_source_class': 'source_valid_under_alternative_denial_target_convention',
                'manual_reason': 'same_actor_explicit_correction_of_old_edge;not_primary_positive_replacement_target',
                'exact_source_turns': citations(event, case), 'primary_scoring_unchanged': True,
                'primary_positive_replacement_convention_match': False})
        rows.append({'case_id': cid, 'family': golds[cid]['family'], 'language': case['language'],
                     'response_sha256': raw['response_sha256'], 'nodes': nodes, 'assertions': assertions, 'events': events})
    assert dict(node_counts) == {'used_reference_atom': 65, 'source_supported_expression_outside_reference': 19, 'unused_reference_control': 4}
    assert dict(edge_counts) == {'source_valid': 38, 'needs_review': 14, 'source_invalid': 10}
    assert sum(event_counts.values()) == 6
    return {'schema': 'loom.free_source_manual_audit/1', 'split': 'dev',
        'post_outcome_secondary_audit': True, 'fixture_author_not_blind': True,
        'primary_scoring_unchanged': True, 'validation_read': False, 'api_calls': 0,
        'attempted_cases': 24, 'complete_raw_graphs_reviewed': 22,
        'excluded_incomplete_cases': excluded, 'reviewed_node_records': 88,
        'node_source_categories': dict(node_counts), 'legitimate_novel_unmatched_reference_paraphrases': 0,
        'reviewed_assertion_records': 62, 'assertion_source_classes': dict(edge_counts),
        'assertion_exact_citation_known_at_bindings': {'valid': 62, 'reviewed': 62},
        'reviewed_status_events': 6, 'status_source_classes': dict(event_counts),
        'not_a_blind_model_quality_measurement': True,
        'no_contract_repair_or_alternative_primary_outputs': True,
        'revision': 2,
        'revision_reason': 'clarify_event_metadata_field;no_measurement_or_class_change;first_audit_preserved',
        'first_audit_sha256': panel.digest_file(HERE / 'manual_source_audit.json'),
        'protocol_sha256': panel.digest_file(HERE / 'MANUAL_AUDIT_PROTOCOL.md'),
        'raw_diagnostics_sha256': panel.digest_file(HERE / 'raw_contract_diagnostics.json'),
        'cases': rows}


if __name__ == '__main__':
    value = build(); output = HERE / 'manual_source_audit2.json'
    if '--check' in sys.argv:
        assert json.loads(output.read_text()) == value
    else:
        with output.open('x') as f:
            json.dump(value, f, ensure_ascii=False, indent=2, sort_keys=True); f.write('\n')
    print(json.dumps({k: v for k, v in value.items() if k != 'cases'}, sort_keys=True))
