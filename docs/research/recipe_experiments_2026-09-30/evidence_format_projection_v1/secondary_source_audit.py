"""Post-output diagnostic join to already saved nonblind manual source labels.

Never chooses projections or model answers. Emits independent strict count
reconciliation and separates semantic adequacy from alias/format failures.
"""
from collections import Counter
from pathlib import Path

from loom.tools.structure import free_evidence_format_projection_v1 as method

free, panel, replay, safe = method.free, method.panel, method.replay, method.safe


def main():
    folder = method.DESTINATION
    projected = replay.read(folder / 'compiled_first.json')
    receipts = replay.read(folder / 'projection_receipts_first.json')
    projected_by = {r['case_id']: r for r in projected}
    receipts_by = {r['case_id']: r for r in receipts}
    old = []
    for batch in ('batch01', 'batch02'):
        old.extend(replay.read(method.BASE / batch / 'first_score/compiled_first.json'))
    old_by = {r['case_id']: r for r in old}
    manual_path = method.ROOT / 'docs/research/graph_free_extraction_v1/manual_source_audit2.json'
    manual = replay.read(manual_path)
    manual_by = {r['case_id']: r for r in manual['cases']}
    cases = {c['id']: c for c in panel.load_dev_inputs()}
    golds = {g['id']: g for g in panel.load_dev_gold()}
    accepted_source = Counter(); new_accepted_source = Counter(); strict_fp_source = Counter()
    new_strict = Counter(); per_case = []; strict = Counter(); event_source = Counter()
    def key(edge):
        return tuple(edge[k] for k in ('relation', 'source', 'target', 'polarity', 'attributed_to', 'known_at'))
    for ident, result in projected_by.items():
        if result['state'] != 'completed':
            strict['fn'] += len(golds[ident]['source_assertions'])
            continue
        source_case = cases[ident]
        mapping, _ = free.align_nodes(result['discovered_nodes'], source_case['node_inventory'])
        remaining = Counter(key(e) for e in golds[ident]['source_assertions'])
        raw = receipts_by[ident]['original_model_object']
        originals = {a['id']: a for a in raw['source_assertions'] if isinstance(a, dict)}
        old_ids = {a['id'] for a in old_by[ident]['source_assertions']}
        annotations = manual_by[ident]['assertions']
        edge_rows = []
        for edge in result['source_assertions']:
            original = originals[edge['id']]
            exact_annotations = [a for a in annotations if a['raw_record'] == original]
            source_class = exact_annotations[0]['manual_source_class'] if len(exact_annotations) == 1 else 'needs_review_unmatched_annotation'
            accepted_source[source_class] += 1
            newly_accepted = edge['id'] not in old_ids
            if newly_accepted: new_accepted_source[source_class] += 1
            source, target = mapping.get(edge['source']), mapping.get(edge['target'])
            if source is None or target is None:
                strict_status = 'unmapped_reference_endpoint'; strict['fp'] += 1
            else:
                projected_key = key(dict(edge, source=source, target=target))
                if remaining[projected_key] > 0:
                    strict_status = 'strict_tp'; strict['tp'] += 1; remaining[projected_key] -= 1
                else:
                    strict_status = 'typed_reference_mismatch_or_duplicate'; strict['fp'] += 1
            if strict_status != 'strict_tp': strict_fp_source[source_class] += 1
            if newly_accepted: new_strict['tp' if strict_status == 'strict_tp' else 'fp'] += 1
            edge_rows.append({'assertion_id': edge['id'], 'newly_accepted_after_format_projection': newly_accepted,
                'strict_reference_status': strict_status, 'saved_manual_source_class': source_class,
                'manual_annotation_matched_exact_original_record': len(exact_annotations) == 1})
        strict['fp'] += result['invalid_assertions']
        strict['fn'] += sum(remaining.values())
        for event in result['status_events']:
            # Event manual categories apply to original records, before conversion.
            found = [r for r in raw['status_events'] if r['assertion_id'] == event['assertion_id']
                and r['superseded_by'] == event['superseded_by'] and r['known_at'] == event['known_at']]
            annotations = [a for a in manual_by[ident]['events'] if a['raw_record'] in found]
            category = annotations[0].get('manual_source_class', 'needs_review') if len(annotations) == 1 else 'needs_review'
            event_source[category] += 1
        per_case.append({'case_id': ident, 'accepted_assertions': edge_rows,
            'invalid_assertions_retained_as_primary_fp': result['invalid_assertions'],
            'accepted_events': len(result['status_events'])})
    primary = replay.read(folder / 'RESULTS.json')['projection']['strict_edges']
    assert all(strict[name] == primary[name] for name in ('tp', 'fp', 'fn'))
    record = {'schema': 'loom.free_evidence_format_projection.secondary_source_audit/1',
        'new_external_calls': 0, 'new_cost_usd': '0',
        'read_manual_labels_only_after_first_projection_outputs': True,
        'manual_source_audit_sha256': panel.digest_file(manual_path),
        'manual_fixture_author_not_blind': manual['fixture_author_not_blind'],
        'independent_strict_count_reconciliation': dict(strict),
        'accepted_assertion_saved_manual_classes': dict(accepted_source),
        'newly_accepted_assertion_saved_manual_classes': dict(new_accepted_source),
        'newly_accepted_strict_reference_counts': dict(new_strict),
        'accepted_strict_fp_saved_manual_classes': dict(strict_fp_source),
        'accepted_events_saved_manual_classes': dict(event_source),
        'per_case': per_case, 'all_primary_invalid_assertions_retained': True,
        'format_binding_not_semantic_validation': True,
        'not_an_independent_population_model_quality_estimate': True,
        'no_validation_read': True}
    panel.write_new(folder / 'SECONDARY_SOURCE_AUDIT.json', record)
    print(safe.canonical({k:v for k,v in record.items() if k != 'per_case'}).decode())


if __name__ == '__main__': main()
