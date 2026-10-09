"""Verify two-pass, source-bound research annotations without inferring a profile.

Exact excerpts and review decisions are private input data. This module validates
and projects reviewed evidence; it performs no semantic classification, automatic
preference adoption, model inference, or network operation.
"""
from __future__ import annotations

from copy import deepcopy
import hashlib

from loom.tools.structure.experiment_context_preparation_v1 import text_projection
from loom.tools.structure.experiment_workflow_v1 import digest, require

VERSION = 'experiment_preference_annotations_v1/1'


def quote_evidence(source, node, quote):
    require(isinstance(quote, str) and bool(quote), 'annotation_quote_missing')
    projection = text_projection(node.get('native_message'))
    matches = [(pointer, value.index(quote)) for pointer, value in
               zip(projection['fields'], projection['parts']) if quote in value]
    require(bool(matches), 'annotation_quote_not_exact')
    pointer, offset = matches[0]
    return {'family_id': source['family_id'], 'source_sha256': source['source_sha256'],
            'source_context_sha256': digest(source), 'node_id': node['node_id'],
            'source_pointer': node.get('source_message_pointer', node['source_pointer']),
            'source_message_sha256': digest(node['native_message']),
            'literal_field_pointer': pointer, 'quote_start_codepoints': offset,
            'quote_end_codepoints': offset + len(quote), 'quote': quote,
            'quote_utf8_sha256': hashlib.sha256(quote.encode('utf-8')).hexdigest(),
            'role': node.get('role'), 'source_time': node.get('source_create_time'),
            'quote_location_count': len(matches)}


def validate_evidence(source, evidence):
    require(evidence['source_sha256'] == source['source_sha256'], 'annotation_source_hash_mismatch')
    require(evidence['source_context_sha256'] == digest(source), 'annotation_context_hash_mismatch')
    by_id = {n['node_id']: n for n in source['nodes']}
    require(evidence['node_id'] in by_id, 'annotation_node_missing')
    expected = quote_evidence(source, by_id[evidence['node_id']], evidence['quote'])
    require(evidence == expected, 'annotation_evidence_mismatch')


def validate_review(sources, pass1, pass2):
    by_family = {s['family_id']: s for s in sources}
    require(pass1['schema'] == 'loom.research_preference_evidence/1', 'annotation_pass1_schema')
    require(pass2['schema'] == 'loom.research_preference_review/1', 'annotation_pass2_schema')
    require(pass2['evidence_sha256'] == digest(pass1), 'annotation_pass2_binding_mismatch')
    require(pass2.get('independent_adjudication') is False, 'annotation_independence_misrepresented')
    coverage = pass1['coverage']
    require(set(coverage) == set(by_family), 'annotation_family_coverage_incomplete')
    for family, row in coverage.items():
        source = by_family[family]
        expected = {n['node_id']: digest(n['native_message']) for n in source['nodes']
                    if n.get('role') == 'user'}
        require(row['source_context_sha256'] == digest(source), 'annotation_coverage_source_mismatch')
        require(row['user_message_inventory'] == expected, 'annotation_message_coverage_incomplete')
        require(bool(row.get('review_method')), 'annotation_review_method_missing')
    records = {r['id']: r for r in pass1['records']}
    require(len(records) == len(pass1['records']), 'annotation_duplicate_evidence')
    reviews = {r['evidence_id']: r for r in pass2['records']}
    require(len(reviews) == len(pass2['records']), 'annotation_duplicate_review')
    require(set(reviews) == set(records), 'annotation_review_incomplete')
    for record in records.values():
        evidence = record['evidence']
        require(evidence['family_id'] in by_family, 'annotation_unknown_family')
        validate_evidence(by_family[evidence['family_id']], evidence)
        interpretation = record['interpretation']
        require(bool(interpretation.get('scope')), 'annotation_scope_missing')
        require(bool(interpretation.get('time')), 'annotation_time_missing')
        require(interpretation.get('adopt_as_profile') is False, 'annotation_profile_adoption_forbidden')
        for rel in interpretation.get('relations', []):
            require(rel['target_evidence_id'] in records, 'annotation_relation_target_missing')
            require(bool(rel['relation']), 'annotation_relation_missing')
        check = reviews[record['id']]
        require(check.get('decision') in ('accept', 'reject', 'uncertain'), 'annotation_review_decision_invalid')
        require(bool(check.get('reason')) and bool(check.get('reviewer')), 'annotation_review_attribution_missing')
        if check['decision'] == 'accept':
            require(evidence['role'] == 'user', 'annotation_accepted_nonuser')
            require(interpretation['utterance_kind'] not in ('quote', 'hypothesis'), 'annotation_unendorsed_as_preference')
    return records, reviews


def project_sources(sources, pass1, pass2):
    records, reviews = validate_review(sources, pass1, pass2)
    outputs = deepcopy(sources)
    for source in outputs:
        original = next(s for s in sources if s['family_id'] == source['family_id'])
        accepted, unresolved, excluded = [], [], []
        for record in records.values():
            evidence = record['evidence']
            if evidence['family_id'] != source['family_id']:
                continue
            review = reviews[record['id']]
            if review['decision'] == 'accept':
                accepted.append({**deepcopy(evidence), 'evidence_id': record['id'],
                                 'classification': 'explicit_preference',
                                 'annotation_authority': 'researcher_annotation',
                                 'scope': deepcopy(record['interpretation']['scope']),
                                 'interpretation': deepcopy(record['interpretation']),
                                 'review': deepcopy(review),
                                 'application': 'historical_scoped_evidence_not_active_instruction'})
            elif review['decision'] == 'uncertain':
                unresolved.append({'evidence_id': record['id'], 'node_id': evidence['node_id'],
                                   'reason': review['reason']})
            else:
                excluded.append({'evidence_id': record['id'], 'reason': review['reason']})
        source['explicit_preference_evidence'] = accepted
        source['preference_review'] = {
            'schema': 'loom.research_preference_source_review/1',
            'status': 'completed', 'source_sha256': source['source_sha256'],
            'input_context_sha256': digest(original), 'evidence_sha256': digest(pass1),
            'review_sha256': digest(pass2), 'reviewer_independence': False,
            'source_message_inventory_sha256': digest({n['node_id']: digest(n['native_message']) for n in source['nodes']}),
            'accepted_evidence_sha256': digest(accepted),
            'uncertain_candidates': unresolved, 'excluded_candidates': excluded,
            'assertion': 'known_scoped_evidence_only_not_exhaustive_semantic_gold',
            'profile_adoption': False, 'view_filter_required': True}
    return outputs


def prepare_reviewed_release(sources, evidence, review, tasks, plan, profiles, output):
    from pathlib import Path
    from loom.tools.structure.experiment_context_preparation_v1 import prepare_release, write_private
    projected = project_sources(sources, evidence, review)
    require(plan.get('annotation_evidence_sha256') == digest(evidence), 'plan_annotation_evidence_mismatch')
    require(plan.get('annotation_review_sha256') == digest(review), 'plan_annotation_review_mismatch')
    manifest = prepare_release(projected, tasks, plan, profiles, output)
    output = Path(output)
    files = {}
    for name, value in [('annotation-inputs/original-sources.json', sources),
                        ('annotation-inputs/evidence.json', evidence),
                        ('annotation-inputs/review.json', review)]:
        files[name] = write_private(output / name, value)
    code = Path(__file__).read_bytes()
    producer = output / 'annotation-inputs/producer.py'
    with producer.open('xb') as stream:
        stream.write(code)
    producer.chmod(0o600)
    files['annotation-inputs/producer.py'] = hashlib.sha256(code).hexdigest()
    wrapper = {'schema': 'loom.research_annotation_preparation_manifest/1',
               'producer': VERSION, 'files': files,
               'preparation_manifest_sha256': hashlib.sha256((output / 'MANIFEST.json').read_bytes()).hexdigest(),
               'source_freeze_mutated': False, 'inference_performed': False,
               'new_cost_usd': '0', 'independent_adjudication': False}
    write_private(output / 'ANNOTATION_MANIFEST.json', wrapper)
    return manifest, wrapper


def main():
    import argparse
    import json
    from pathlib import Path
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('sources', 'evidence', 'review', 'tasks', 'plan', 'profiles', 'output'):
        parser.add_argument('--' + key, required=True)
    args = parser.parse_args()
    data = {key: json.loads(Path(getattr(args, key)).read_text()) for key in
            ('sources', 'evidence', 'review', 'tasks', 'plan', 'profiles')}
    manifest, wrapper = prepare_reviewed_release(**data, output=args.output)
    print(json.dumps({'preparation_manifest_sha256': wrapper['preparation_manifest_sha256'],
                      'inference_performed': False, 'new_cost_usd': '0'}))


if __name__ == '__main__':
    main()
